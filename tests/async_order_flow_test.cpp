#include <iostream>

#include "exec/adapter/sim_adapter.hpp"
#include "exec/engine/execution_engine.hpp"
#include "exec/state/execution_state_view.hpp"

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

exec::SetBasketTargetCommand make_target_command(const exec::BasketId& basket_id, exec::Quantity target_qty) {
    exec::SetBasketTargetCommand command;
    command.basket_id = basket_id;
    command.strategy_id = "test-strategy";
    command.account_id = "test-account";
    command.legs = {
        exec::LegTarget{
            .instrument_id = "A",
            .long_goal = exec::GoalExpression::set_quantity(target_qty),
        },
    };
    return command;
}

exec::ExecutionResult drain_adapter_reports(exec::ExecutionEngine& engine, exec::SimAdapter& adapter) {
    exec::ExecutionResult result;
    while (adapter.has_pending_reports()) {
        result = engine.on_execution_reports(adapter.drain_reports());
    }
    return result;
}

}  // 匿名命名空间

int main() {
    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        exec::SimAdapter adapter(state);
        adapter.push_scripted_reports({
            exec::ExecutionReport{.status = exec::OrderStatus::New, .text = "只 ack"},
        });
        exec::ExecutionEngine engine(state, adapter);

        auto submit = engine.submit(make_target_command("async-fill", 5));
        if (!expect(submit.status == exec::BasketStatus::Active, "ack-only 后 basket 应保持 Active") ||
            !expect(state.has_working_order("A"), "ack-only 后应保留在途订单")) {
            return 1;
        }
        submit = drain_adapter_reports(engine, adapter);
        if (!expect(submit.status == exec::BasketStatus::Active, "ack 回报后 basket 应继续等待成交")) {
            return 1;
        }

        const auto report = engine.on_execution_report(exec::ExecutionReport{
            .order_id = 1,
            .trade_id = "T1",
            .status = exec::OrderStatus::Filled,
            .last_qty = 5,
            .cumulative_qty = 5,
            .last_price = 10,
            .text = "异步全成",
        });
        if (!expect(report.status == exec::BasketStatus::Complete, "异步全成后 basket 应完成") ||
            !expect(state.effective_long("A") == 5, "异步全成后持仓应更新") ||
            !expect(state.effective_cash() == 50, "异步全成后现金应更新") ||
            !expect(!state.has_working_order("A"), "异步全成后不应还有在途订单")) {
            return 1;
        }
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        exec::SimAdapter adapter(state);
        adapter.push_scripted_reports({
            exec::ExecutionReport{.status = exec::OrderStatus::New, .text = "只 ack"},
        });
        exec::ExecutionEngine engine(state, adapter);

        auto submit = engine.submit(make_target_command("async-reject", 5));
        if (!expect(submit.status == exec::BasketStatus::Active, "等待拒单回报时 basket 应保持 Active")) {
            return 1;
        }
        submit = drain_adapter_reports(engine, adapter);
        if (!expect(submit.status == exec::BasketStatus::Active, "ack 回报后仍应等待拒单或成交")) {
            return 1;
        }

        const auto rejected = engine.on_execution_report(exec::ExecutionReport{
            .order_id = 1,
            .status = exec::OrderStatus::Rejected,
            .text = "异步拒单",
        });
        if (!expect(rejected.status == exec::BasketStatus::Failed, "异步拒单后 active basket 应失败") ||
            !expect(state.effective_long("A") == 0, "异步拒单不应改变持仓") ||
            !expect(state.effective_cash() == 100, "异步拒单应释放现金预占") ||
            !expect(!state.has_working_order("A"), "异步拒单后不应还有在途订单")) {
            return 1;
        }
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        exec::SimAdapter adapter(state);
        adapter.push_scripted_reports({
            exec::ExecutionReport{.status = exec::OrderStatus::New, .text = "只 ack"},
        });
        exec::ExecutionEngine engine(state, adapter);

        auto submit = engine.submit(make_target_command("cancel-replan", 5));
        if (!expect(submit.status == exec::BasketStatus::Active, "等待异步回报时 basket 应保持 Active")) {
            return 1;
        }
        submit = drain_adapter_reports(engine, adapter);
        if (!expect(submit.status == exec::BasketStatus::Active, "ack 回报后订单仍应在途")) {
            return 1;
        }

        const auto partial = engine.on_execution_report(exec::ExecutionReport{
            .order_id = 1,
            .trade_id = "T2",
            .status = exec::OrderStatus::PartiallyFilled,
            .last_qty = 3,
            .cumulative_qty = 3,
            .last_price = 10,
            .text = "异步部分成交",
        });
        if (!expect(partial.status == exec::BasketStatus::Active, "部分成交后订单仍在途") ||
            !expect(state.effective_long("A") == 3, "部分成交后持仓应为 3") ||
            !expect(state.effective_cash() == 50, "部分成交后可用现金应扣除成交和剩余预占") ||
            !expect(state.has_working_order("A"), "部分成交后剩余数量仍应在途")) {
            return 1;
        }

        auto canceled = engine.on_execution_report(exec::ExecutionReport{
            .order_id = 1,
            .status = exec::OrderStatus::Canceled,
            .text = "撤单成功",
        });
        if (!expect(canceled.status == exec::BasketStatus::Active, "撤单释放后应重规划剩余缺口并等待新回报") ||
            !expect(state.effective_long("A") == 3, "新订单成交前持仓仍应为 3") ||
            !expect(state.has_working_order("A"), "重规划新订单应处于在途状态")) {
            return 1;
        }

        canceled = drain_adapter_reports(engine, adapter);
        if (!expect(canceled.status == exec::BasketStatus::Complete, "新订单成交后 basket 应完成") ||
            !expect(state.effective_long("A") == 5, "重规划后持仓应达到目标") ||
            !expect(state.effective_cash() == 50, "重规划后现金应正确") ||
            !expect(!state.has_working_order("A"), "重规划完成后不应还有在途订单")) {
            return 1;
        }
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        exec::SimAdapter adapter(state);
        adapter.push_scripted_reports({
            exec::ExecutionReport{
                .venue_order_id = "V1",
                .status = exec::OrderStatus::New,
                .text = "venue ack",
            },
        });
        exec::ExecutionEngine engine(state, adapter);

        exec::BasketActionCommand command;
        command.basket_id = "venue-fill";
        command.strategy_id = "test-strategy";
        command.account_id = "test-account";
        command.actions = {
            exec::PositionOrderIntent{
                .instrument_id = "A",
                .side = exec::TradeSide::Buy,
                .bucket = exec::PositionBucket::Long,
                .quantity = 5,
            },
        };

        auto submit = engine.submit(command);
        if (!expect(submit.status == exec::BasketStatus::Active, "venue ack 后直接动作应等待回报")) {
            return 1;
        }
        submit = drain_adapter_reports(engine, adapter);
        if (!expect(submit.status == exec::BasketStatus::Active, "venue ack 处理后直接动作仍应等待成交")) {
            return 1;
        }

        const auto fill = engine.on_execution_report(exec::ExecutionReport{
            .venue_order_id = "V1",
            .trade_id = "T3",
            .status = exec::OrderStatus::Filled,
            .last_qty = 5,
            .cumulative_qty = 5,
            .last_price = 10,
            .text = "venue fill",
        });
        (void)engine.on_execution_report(exec::ExecutionReport{
            .venue_order_id = "V1",
            .trade_id = "T3",
            .status = exec::OrderStatus::Filled,
            .last_qty = 5,
            .cumulative_qty = 5,
            .last_price = 10,
            .text = "重复成交",
        });

        if (!expect(fill.status == exec::BasketStatus::Complete, "venue id 异步成交应成功匹配订单") ||
            !expect(state.effective_long("A") == 5, "重复成交不应重复加仓") ||
            !expect(state.effective_cash() == 50, "重复成交不应重复扣现金")) {
            return 1;
        }
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(1000);
        state.set_position("A", 0);
        state.set_mark_price("A", 100);
        state.set_book_top("A", exec::BookTop{.bid_price = 99, .bid_qty = 100, .ask_price = 101, .ask_qty = 50});

        exec::SimAdapter adapter(state);
        exec::ExecutionEngine engine(state, adapter);

        auto command = make_target_command("wonder-price", 5);
        command.default_style.price_model_id = exec::PriceModelId::ImbalanceTopOfBook;

        auto result = engine.submit(command);
        if (!expect(result.status == exec::BasketStatus::Active, "ImbalanceTopOfBook 应提交限价子单")) {
            return 1;
        }
        result = drain_adapter_reports(engine, adapter);
        if (!expect(result.status == exec::BasketStatus::Complete, "ImbalanceTopOfBook 成交后 basket 应完成") ||
            !expect(state.effective_long("A") == 5, "ImbalanceTopOfBook 成交后持仓应更新") ||
            !expect(state.effective_cash() == 495, "盘口偏多时 ImbalanceTopOfBook 买入应按 ask 成交")) {
            return 1;
        }
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(1000);
        state.set_position("A", 0);
        state.set_mark_price("A", 100);
        state.set_book_top("A", exec::BookTop{.bid_price = 99, .bid_qty = 50, .ask_price = 101, .ask_qty = 100});

        exec::SimAdapter adapter(state);
        exec::ExecutionEngine engine(state, adapter);

        auto command = make_target_command("passive-price", 5);
        command.default_style.price_model_id = exec::PriceModelId::PassiveTopOfBook;

        auto result = engine.submit(command);
        if (!expect(result.status == exec::BasketStatus::Active, "PassiveTopOfBook 应提交 best bid 限价子单")) {
            return 1;
        }
        result = drain_adapter_reports(engine, adapter);
        if (!expect(result.status == exec::BasketStatus::Complete, "PassiveTopOfBook 成交后 basket 应完成") ||
            !expect(state.effective_cash() == 505, "PassiveTopOfBook 买入模拟成交价应为 best bid")) {
            return 1;
        }
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(1000);
        state.set_position("A", 0);
        state.set_mark_price("A", 100);
        state.set_book_top("A", exec::BookTop{.bid_price = 99, .bid_qty = 100, .ask_price = 101, .ask_qty = 50});

        exec::SimAdapter adapter(state);
        exec::ExecutionEngine engine(state, adapter);

        exec::BasketActionCommand command;
        command.basket_id = "explicit-price";
        command.strategy_id = "test-strategy";
        command.account_id = "test-account";
        command.actions = {
            exec::PositionOrderIntent{
                .instrument_id = "A",
                .side = exec::TradeSide::Buy,
                .bucket = exec::PositionBucket::Long,
                .quantity = 5,
                .order = exec::OrderSpec{
                    .type = exec::OrderType::Limit,
                    .tif = exec::TimeInForce::Gtc,
                    .limit_price = 42,
                },
                .style = exec::ExecutionStyle{.price_model_id = exec::PriceModelId::ImbalanceTopOfBook},
            },
        };

        auto result = engine.submit(command);
        if (!expect(result.status == exec::BasketStatus::Active, "显式限价应提交成功")) {
            return 1;
        }
        result = drain_adapter_reports(engine, adapter);
        if (!expect(result.status == exec::BasketStatus::Complete, "显式限价成交后应完成") ||
            !expect(state.effective_cash() == 790, "显式 limit_price 不应被价格模型覆盖")) {
            return 1;
        }
    }

    return 0;
}
