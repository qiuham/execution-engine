#include "exec/engine/execution_engine.hpp"

#include <cmath>
#include <memory>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>

namespace exec {

namespace {

Quantity round_to_quantity(double value) {
    return static_cast<Quantity>(std::llround(value));
}

Quantity floor_to_quantity(double value) {
    if (value >= 0) {
        return static_cast<Quantity>(std::floor(value));
    }
    return static_cast<Quantity>(std::ceil(value));
}

bool goal_needs_price(const GoalExpression& goal) {
    return goal.op != GoalOp::Keep && goal.value != 0.0 &&
           (goal.measure == GoalMeasure::Notional || goal.measure == GoalMeasure::Weight);
}

std::string describe_intent(const PositionOrderIntent& intent) {
    std::ostringstream out;
    out << to_string(intent.side) << ' ' << to_string(intent.bucket) << ' ' << intent.quantity << ' '
        << intent.instrument_id;
    out << " order=" << to_string(intent.order.type) << '/' << to_string(intent.order.tif);
    if (intent.order.limit_price.has_value()) {
        out << " limit=" << *intent.order.limit_price;
    }
    if (intent.order.reservation_price.has_value()) {
        out << " reserve=" << *intent.order.reservation_price;
    }
    if (intent.order.post_only) {
        out << " post_only";
    }
    return out.str();
}

}  // 匿名命名空间

ExecutionEngine::ExecutionEngine(ExecutionStateView& state, IVenueAdapter& adapter)
    : ExecutionEngine(state, adapter, std::make_shared<RuleBasedChildOrderPricer>()) {}

ExecutionEngine::ExecutionEngine(ExecutionStateView& state,
                                 IVenueAdapter& adapter,
                                 std::shared_ptr<IChildOrderPricer> pricer)
    : state_(state),
      adapter_(adapter),
      pricer_(std::move(pricer)) {
    if (!pricer_) {
        pricer_ = std::make_shared<RuleBasedChildOrderPricer>();
    }
}

ExecutionResult ExecutionEngine::submit(const SetBasketTargetCommand& command) {
    ExecutionResult result;

    if (trading_state_ == TradingState::Halted || trading_state_ == TradingState::Killed) {
        result.status = BasketStatus::Rejected;
        result.logs.push_back("basket 被拒绝: 当前交易状态=" + std::string(to_string(trading_state_)));
        return result;
    }

    auto basket = resolve(command);

    std::string reject_reason;
    if (!validate_v1(basket, reject_reason)) {
        basket.status = BasketStatus::Rejected;
        result.status = basket.status;
        result.logs.push_back("basket 被拒绝: " + reject_reason);
        return result;
    }

    active_target_commands_[command.basket_id] = command;
    basket.status = BasketStatus::Active;
    result.status = basket.status;
    result.logs.push_back("basket " + basket.basket_id + " active");

    // 同步原型每轮都会重新 resolve，后续权重/名义金额目标可以跟随最新成交和现金视图动态换算。
    for (int iteration = 0; iteration < 32; ++iteration) {
        if (iteration > 0) {
            basket = resolve(command);
            if (!validate_v1(basket, reject_reason)) {
                result.status = BasketStatus::Failed;
                result.logs.push_back("basket 失败: 重规划时目标解析失败: " + reject_reason);
                active_target_commands_.erase(command.basket_id);
                return result;
            }
            basket.status = BasketStatus::Active;
        }

        const auto decision = planner_.plan_next(basket, state_);
        for (const auto& note : decision.notes) {
            result.logs.push_back("planner 备注: " + note);
        }

        if (decision.intents.empty()) {
            if (has_working_orders(basket)) {
                result.status = BasketStatus::Active;
                result.logs.push_back("basket " + basket.basket_id + " 等待在途订单回报");
                return result;
            }
            if (decision.complete) {
                basket.status = BasketStatus::Complete;
                result.status = basket.status;
                result.logs.push_back("basket " + basket.basket_id + " complete");
                active_target_commands_.erase(basket.basket_id);
                return result;
            }

            basket.status = BasketStatus::Failed;
            result.status = basket.status;
            for (const auto& reason : decision.blocked_reasons) {
                result.logs.push_back("planner 阻塞: " + reason);
            }
            if (decision.blocked_reasons.empty()) {
                result.logs.push_back("basket 失败: 无可发子单，但目标尚未完成");
            }
            active_target_commands_.erase(basket.basket_id);
            return result;
        }

        for (const auto& reason : decision.blocked_reasons) {
            result.logs.push_back("planner 阻塞: " + reason);
        }
        result.logs.push_back("planner 生成 " + std::to_string(decision.intents.size()) + " 个子单");
        bool submitted_any = false;
        for (const auto& intent : decision.intents) {
            submitted_any = submit_child_order(basket, intent, result) || submitted_any;
        }

        if (!submitted_any) {
            basket.status = BasketStatus::Failed;
            result.status = basket.status;
            result.logs.push_back("basket 失败: 本轮子单全部被交易状态或资源预占拒绝");
            active_target_commands_.erase(basket.basket_id);
            return result;
        }

        if (has_working_orders(basket)) {
            result.status = BasketStatus::Active;
            result.logs.push_back("basket " + basket.basket_id + " 等待在途订单回报");
            return result;
        }
    }

    basket.status = BasketStatus::Failed;
    result.status = basket.status;
    result.logs.push_back("basket 失败: planner 达到迭代上限");
    active_target_commands_.erase(basket.basket_id);
    return result;
}

ExecutionResult ExecutionEngine::submit(const BasketActionCommand& command) {
    ExecutionResult result;
    if (command.actions.empty()) {
        result.status = BasketStatus::Rejected;
        result.logs.push_back("直接动作被拒绝: actions 为空");
        return result;
    }

    BasketExecution basket;
    basket.basket_id = command.basket_id;
    basket.strategy_id = command.strategy_id;
    basket.account_id = command.account_id;
    basket.status = BasketStatus::Active;

    bool submitted_any = false;
    for (const auto& action : command.actions) {
        if (action.quantity <= 0) {
            result.logs.push_back("直接动作跳过: 数量必须为正 " + describe_intent(action));
            continue;
        }
        if (action.bucket != PositionBucket::Long) {
            result.logs.push_back("直接动作跳过: V1 只支持 LONG bucket " + describe_intent(action));
            continue;
        }
        submitted_any = submit_child_order(basket, action, result) || submitted_any;
    }

    bool waiting = false;
    for (const auto& action : command.actions) {
        waiting = waiting || state_.has_working_order(action.instrument_id);
    }
    result.status = submitted_any ? (waiting ? BasketStatus::Active : BasketStatus::Complete) : BasketStatus::Rejected;
    if (waiting) {
        result.logs.push_back("直接动作已提交，等待在途订单回报");
    }
    return result;
}

ExecutionResult ExecutionEngine::control(const ControlCommand& command) {
    ExecutionResult result;
    result.status = BasketStatus::Complete;

    switch (command.action) {
        case ControlAction::Pause:
            set_trading_state(TradingState::Halted);
            result.logs.push_back("控制命令: 交易状态切换到 Halted");
            break;
        case ControlAction::Resume:
            set_trading_state(TradingState::Active);
            result.logs.push_back("控制命令: 交易状态切换到 Active");
            break;
        case ControlAction::ReduceOnly:
            set_trading_state(TradingState::Reducing);
            result.logs.push_back("控制命令: 交易状态切换到 Reducing");
            break;
        case ControlAction::KillSwitch:
            set_trading_state(TradingState::Killed);
            result.logs.push_back("控制命令: kill switch 触发，交易状态切换到 Killed");
            break;
        case ControlAction::CancelBasket:
            result.logs.push_back("控制命令: CancelBasket 已接收，真实撤单链路后续接入");
            break;
        case ControlAction::CancelAll:
            result.logs.push_back("控制命令: CancelAll 已接收，真实撤单链路后续接入");
            break;
    }

    return result;
}

ExecutionResult ExecutionEngine::handle(const ExecutionCommand& command) {
    return std::visit(
        [this](const auto& value) -> ExecutionResult {
            using Command = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Command, ControlCommand>) {
                return control(value);
            } else {
                return submit(value);
            }
        },
        command);
}

ExecutionResult ExecutionEngine::on_execution_report(const ExecutionReport& report) {
    ExecutionResult result;
    auto* order = order_store_.find_for_report(report);
    if (order == nullptr) {
        result.status = BasketStatus::Rejected;
        result.logs.push_back("订单回报无法匹配本地订单，已忽略");
        return result;
    }

    const auto basket_id = order->basket_id;
    const auto was_terminal = is_terminal(order->status);
    apply_order_report(*order, report, result);
    if (!is_terminal(order->status) && open_quantity(*order) > 0) {
        result.logs.push_back("子单 " + std::to_string(order->order_id) + " 仍有在途数量 " +
                              std::to_string(open_quantity(*order)));
    }

    result.status = is_terminal(order->status) ? BasketStatus::Complete : BasketStatus::Active;
    if (!was_terminal && is_terminal(order->status)) {
        if (order->status == OrderStatus::Rejected) {
            active_target_commands_.erase(basket_id);
            result.status = BasketStatus::Failed;
            result.logs.push_back("basket " + basket_id + " 因异步拒单停止自动重规划");
            return result;
        }

        const auto it = active_target_commands_.find(basket_id);
        if (it != active_target_commands_.end()) {
            const auto command = it->second;
            auto continuation = submit(command);
            result.status = continuation.status;
            for (const auto& line : continuation.logs) {
                result.logs.push_back("重规划: " + line);
            }
        }
    }

    return result;
}

ExecutionResult ExecutionEngine::on_execution_reports(const std::vector<ExecutionReport>& reports) {
    ExecutionResult result;
    result.status = BasketStatus::Complete;
    for (const auto& report : reports) {
        auto one = on_execution_report(report);
        result.status = one.status;
        result.logs.insert(result.logs.end(), one.logs.begin(), one.logs.end());
    }
    return result;
}

void ExecutionEngine::set_trading_state(TradingState state) {
    trading_state_ = state;
}

TradingState ExecutionEngine::trading_state() const {
    return trading_state_;
}

BasketExecution ExecutionEngine::resolve(const SetBasketTargetCommand& command) const {
    BasketExecution basket;
    basket.basket_id = command.basket_id;
    basket.strategy_id = command.strategy_id;
    basket.account_id = command.account_id;
    basket.plan_policy = command.plan_policy;
    basket.default_style = command.default_style;

    const auto total_equity = state_.total_equity();
    for (const auto& leg : command.legs) {
        LegExecution resolved;
        resolved.instrument_id = leg.instrument_id;
        resolved.constraints = leg.constraints;
        resolved.style = leg.style_override.value_or(command.default_style);
        resolved.priority = leg.priority;

        const auto mark = state_.mark_price(leg.instrument_id);
        if (goal_needs_price(leg.long_goal) && mark <= 0) {
            basket.reject_reason = "目标 " + leg.instrument_id + " 使用名义金额/权重，但缺少 mark price";
        }
        if (goal_needs_price(leg.short_goal) && mark <= 0) {
            basket.reject_reason = "short 目标 " + leg.instrument_id + " 使用名义金额/权重，但缺少 mark price";
        }

        resolved.target_long_qty = resolve_goal_quantity(
            leg.long_goal, state_.effective_long(leg.instrument_id), mark, total_equity);
        resolved.target_short_qty = resolve_goal_quantity(leg.short_goal, 0, mark, total_equity);
        basket.legs.push_back(resolved);
    }

    return basket;
}

Quantity ExecutionEngine::resolve_goal_quantity(const GoalExpression& goal,
                                                Quantity current_qty,
                                                Price mark_price,
                                                Notional total_equity) const {
    if (goal.op == GoalOp::Keep) {
        return current_qty;
    }

    Quantity goal_qty = 0;
    if (goal.measure == GoalMeasure::Quantity) {
        goal_qty = round_to_quantity(goal.value);
    } else if (mark_price > 0 && goal.measure == GoalMeasure::Notional) {
        goal_qty = floor_to_quantity(goal.value / static_cast<double>(mark_price));
    } else if (mark_price > 0 && goal.measure == GoalMeasure::Weight) {
        goal_qty = floor_to_quantity((static_cast<double>(total_equity) * goal.value) /
                                     static_cast<double>(mark_price));
    }

    if (goal.op == GoalOp::ChangeBy) {
        return current_qty + goal_qty;
    }
    return goal_qty;
}

bool ExecutionEngine::validate_v1(const BasketExecution& basket, std::string& reason) const {
    if (!basket.reject_reason.empty()) {
        reason = basket.reject_reason;
        return false;
    }
    if (basket.legs.empty()) {
        reason = "basket 至少需要包含一个 leg";
        return false;
    }
    for (const auto& leg : basket.legs) {
        if (leg.instrument_id.empty()) {
            reason = "leg instrument_id 为空";
            return false;
        }
        if (leg.constraints.lot_size <= 0) {
            reason = "leg " + leg.instrument_id + " lot_size 必须为正";
            return false;
        }
        if (leg.constraints.qty_tolerance < 0 || leg.constraints.min_order_qty < 0 ||
            leg.constraints.min_order_notional < 0) {
            reason = "leg " + leg.instrument_id + " 约束不能为负数";
            return false;
        }
        if (leg.target_long_qty < 0) {
            reason = "V1 不支持负数 long 目标";
            return false;
        }
        if (leg.target_short_qty != 0) {
            reason = "V1 不支持 short 目标";
            return false;
        }
    }
    return true;
}

bool ExecutionEngine::has_working_orders(const BasketExecution& basket) const {
    for (const auto& leg : basket.legs) {
        if (state_.has_working_order(leg.instrument_id)) {
            return true;
        }
    }
    return false;
}

bool ExecutionEngine::submit_child_order(const BasketExecution& basket,
                                         const PositionOrderIntent& intent,
                                         ExecutionResult& result) {
    if (!allows_new_order(trading_state_, intent)) {
        result.logs.push_back("子单被交易状态拒绝: " + describe_intent(intent) + " 原因=" +
                              trading_state_reject_reason(trading_state_, intent));
        return false;
    }

    const auto price_decision = pricer_->price(intent, state_);
    if (!price_decision.accepted) {
        result.logs.push_back("子单报价失败: " + describe_intent(intent) + " 模型=" +
                              price_decision.model_name + " 原因=" + price_decision.reason);
        return false;
    }

    const auto& priced_intent = price_decision.intent;
    result.logs.push_back("报价模型 " + price_decision.model_name + ": " + describe_intent(priced_intent));

    const auto reservation = state_.reserve_for_submit(priced_intent);
    if (!reservation.ok) {
        result.logs.push_back("子单发送前被拒绝: " + describe_intent(priced_intent) + " 原因=" + reservation.reason);
        return false;
    }

    const auto order_id = next_order_id_++;
    auto& order = order_store_.create_order(order_id, basket.basket_id, priced_intent);

    result.logs.push_back("发送子单 " + std::to_string(order.order_id) + ": " + describe_intent(priced_intent));
    const auto send_result = adapter_.send_order(order);
    if (!send_result.accepted) {
        result.logs.push_back("交易通道拒绝接收子单 " + std::to_string(order.order_id) + ": " + send_result.text);
        apply_order_report(order,
                           ExecutionReport{
                               .order_id = order.order_id,
                               .status = OrderStatus::Rejected,
                               .text = send_result.text.empty() ? "交易通道拒绝接收" : send_result.text,
                           },
                           result);
        return false;
    }

    result.logs.push_back("子单 " + std::to_string(order.order_id) + " 已提交，等待交易通道回报");
    return true;
}

void ExecutionEngine::apply_order_report(ChildOrder& order,
                                         const ExecutionReport& report,
                                         ExecutionResult& result) {
    const auto transition = order_state_machine_.apply(order, report);
    if (!report.venue_order_id.empty()) {
        order_store_.bind_venue_order_id(order.order_id, report.venue_order_id);
    }

    if (!transition.text.empty()) {
        result.logs.push_back("子单回报 " + std::to_string(order.order_id) + ": " + transition.text);
    }

    if (transition.fill_qty > 0) {
        auto fill_price = transition.fill_price;
        if (fill_price <= 0) {
            fill_price = state_.mark_price(order.intent.instrument_id);
        }
        state_.apply_fill(order.intent, transition.fill_qty, fill_price);
        result.logs.push_back("子单成交 " + std::to_string(order.order_id) + ": qty=" +
                              std::to_string(transition.fill_qty) + " price=" + std::to_string(fill_price));
    }

    release_terminal_order(order, result);
}

void ExecutionEngine::release_terminal_order(ChildOrder& order, ExecutionResult& result) {
    if (!is_terminal(order.status) || order.open_quantity_released) {
        return;
    }

    const auto unfilled = open_quantity(order);
    if (unfilled > 0) {
        state_.release_unfilled(order.intent, unfilled);
        result.logs.push_back("子单释放未成交资源 " + std::to_string(order.order_id) + ": qty=" +
                              std::to_string(unfilled) + " status=" + to_string(order.status));
    }
    order.open_quantity_released = true;
}

}  // 命名空间 exec
