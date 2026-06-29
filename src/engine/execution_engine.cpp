#include "exec/engine/execution_engine.hpp"

#include <cmath>
#include <sstream>

namespace exec {

namespace {
Quantity as_quantity(double value) {
    return static_cast<Quantity>(std::llround(value));
}

std::string describe_intent(const PositionOrderIntent& intent) {
    std::ostringstream out;
    out << to_string(intent.side) << ' ' << to_string(intent.bucket) << ' ' << intent.quantity << ' '
        << intent.instrument_id;
    return out.str();
}
}  // 匿名命名空间

ExecutionEngine::ExecutionEngine(ExecutionStateView& state, IVenueAdapter& adapter)
    : state_(state), adapter_(adapter) {}

ExecutionResult ExecutionEngine::submit(const SetBasketTargetCommand& command) {
    ExecutionResult result;
    auto basket = resolve(command);

    std::string reject_reason;
    if (!validate_v1(basket, reject_reason)) {
        basket.status = BasketStatus::Rejected;
        result.status = basket.status;
        result.logs.push_back("basket 被拒绝: " + reject_reason);
        return result;
    }

    basket.status = BasketStatus::Active;
    result.logs.push_back("basket " + basket.basket_id + " active");

    // V1 为了便于理解先使用同步流程：规划一个阶段、发送模拟子单、处理回报，
    // 然后继续重规划，直到没有剩余缺口。
    for (int iteration = 0; iteration < 32; ++iteration) {
        const auto intents = planner_.plan_next(basket, state_);
        if (intents.empty()) {
            basket.status = BasketStatus::Complete;
            result.status = basket.status;
            result.logs.push_back("basket " + basket.basket_id + " complete");
            return result;
        }

        result.logs.push_back("planner 生成 " + std::to_string(intents.size()) + " 个子单");
        for (const auto& intent : intents) {
            submit_child_order(basket, intent, result);
        }
    }

    basket.status = BasketStatus::Failed;
    result.status = basket.status;
    result.logs.push_back("basket 失败: planner 达到迭代上限");
    return result;
}

BasketExecution ExecutionEngine::resolve(const SetBasketTargetCommand& command) const {
    BasketExecution basket;
    basket.basket_id = command.basket_id;
    basket.strategy_id = command.strategy_id;
    basket.account_id = command.account_id;
    basket.plan_policy = command.plan_policy;
    basket.default_style = command.default_style;

    for (const auto& leg : command.legs) {
        LegExecution resolved;
        resolved.instrument_id = leg.instrument_id;
        resolved.style = leg.style_override.value_or(command.default_style);
        resolved.priority = leg.priority;

        if (leg.long_goal.op == GoalOp::SetTo && leg.long_goal.measure == GoalMeasure::Quantity) {
            resolved.target_long_qty = as_quantity(leg.long_goal.value);
        }
        if (leg.short_goal.op == GoalOp::SetTo && leg.short_goal.measure == GoalMeasure::Quantity) {
            resolved.target_short_qty = as_quantity(leg.short_goal.value);
        }
        basket.legs.push_back(resolved);
    }

    return basket;
}

bool ExecutionEngine::validate_v1(const BasketExecution& basket, std::string& reason) const {
    if (basket.legs.empty()) {
        reason = "basket 至少需要包含一个 leg";
        return false;
    }
    for (const auto& leg : basket.legs) {
        if (leg.instrument_id.empty()) {
            reason = "leg instrument_id 为空";
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

void ExecutionEngine::submit_child_order(const BasketExecution& basket,
                                         const PositionOrderIntent& intent,
                                         ExecutionResult& result) {
    const auto reservation = state_.reserve_for_submit(intent);
    if (!reservation.ok) {
        result.logs.push_back("子单发送前被拒绝: " + describe_intent(intent) + " 原因=" + reservation.reason);
        return;
    }

    ChildOrder order;
    order.order_id = next_order_id_++;
    order.basket_id = basket.basket_id;
    order.intent = intent;

    result.logs.push_back("发送子单 " + std::to_string(order.order_id) + ": " + describe_intent(intent));
    auto report = adapter_.send_order(order);
    order_state_machine_.apply(order, report);

    if (report.status == OrderStatus::Filled || report.status == OrderStatus::PartiallyFilled) {
        state_.apply_fill(intent, report.last_qty, report.last_price);
        result.logs.push_back("子单成交 " + std::to_string(order.order_id) + ": qty=" +
                              std::to_string(report.last_qty) + " price=" + std::to_string(report.last_price));
        return;
    }

    if (report.status == OrderStatus::Rejected || report.status == OrderStatus::Canceled) {
        state_.release_rejected(intent);
        result.logs.push_back("子单未成交 " + std::to_string(order.order_id) + ": " + report.text);
    }
}

}  // 命名空间 exec
