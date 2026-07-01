#include "test_support.hpp"

#include <vector>

#include "exec/adapter/sim_adapter.hpp"
#include "exec/engine/execution_engine.hpp"

namespace {

exec::SetBasketTargetCommand make_target_command(const exec::BasketId& basket_id,
                                                 exec::Quantity target_qty,
                                                 const exec::InstrumentId& instrument_id = "A") {
    exec::SetBasketTargetCommand command;
    command.basket_id = basket_id;
    command.strategy_id = "test-strategy";
    command.account_id = "test-account";
    command.legs = {
        exec::LegTarget{
            .instrument_id = instrument_id,
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

class RejectCancelAdapter final : public exec::IVenueAdapter {
public:
    exec::SendOrderResult send_order(const exec::ChildOrder& order) override {
        pending_reports_.push_back(exec::ExecutionReport{
            .order_id = order.order_id,
            .status = exec::OrderStatus::New,
            .text = "ack",
        });
        return {.accepted = true, .text = "accepted"};
    }

    exec::SendOrderResult cancel_order(const exec::ChildOrder& order) override {
        (void)order;
        return {.accepted = false, .text = "cancel rejected by adapter"};
    }

    std::vector<exec::ExecutionReport> drain_reports() {
        auto reports = std::move(pending_reports_);
        pending_reports_.clear();
        return reports;
    }

private:
    std::vector<exec::ExecutionReport> pending_reports_;
};

}  // 匿名命名空间

int main() {
    exec::test::TestContext ctx;

    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        exec::SimAdapter adapter(state);
        exec::ExecutionEngine engine(state, adapter);

        exec::ControlCommand cancel;
        cancel.action = exec::ControlAction::CancelBasket;
        const auto result = engine.control(cancel);
        ctx.expect(result.status == exec::BasketStatus::Rejected, "CancelBasket 缺少 basket_id 应拒绝");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        exec::SimAdapter adapter(state);
        adapter.push_scripted_reports({
            exec::ExecutionReport{.status = exec::OrderStatus::New, .text = "ack"},
        });
        exec::ExecutionEngine engine(state, adapter);

        auto result = engine.submit(make_target_command("cancel-open", 5));
        ctx.expect(result.status == exec::BasketStatus::Active, "ack-only 目标单提交后应 Active");
        result = drain_adapter_reports(engine, adapter);
        ctx.expect(result.status == exec::BasketStatus::Active, "ack-only 回报后应等待成交或撤单");
        ctx.expect_eq(state.effective_cash(), 50, "未成交买单应占用现金");

        exec::ControlCommand cancel;
        cancel.action = exec::ControlAction::CancelBasket;
        cancel.basket_id = "cancel-open";
        result = engine.control(cancel);
        ctx.expect(result.status == exec::BasketStatus::Active, "首次 CancelBasket 应发出撤单并等待回报");
        ctx.expect_eq(state.effective_cash(), 50, "PendingCancel 不应释放现金");

        result = engine.control(cancel);
        ctx.expect(result.status == exec::BasketStatus::Complete,
                   "PendingCancel 中重复 CancelBasket 应幂等完成而不是误报失败");
        ctx.expect_eq(state.effective_cash(), 50, "重复撤单不应改变 reservation");

        result = drain_adapter_reports(engine, adapter);
        ctx.expect(result.status == exec::BasketStatus::Complete, "撤单成功回报后控制流程应完成");
        ctx.expect_eq(state.effective_cash(), 100, "撤单成功后应释放现金预占");
        ctx.expect(!state.has_working_order("A"), "撤单成功后不应还有在途订单");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        exec::SimAdapter adapter(state);
        adapter.push_scripted_reports({
            exec::ExecutionReport{.status = exec::OrderStatus::New, .text = "ack"},
        });
        exec::ExecutionEngine engine(state, adapter);

        auto result = engine.submit(make_target_command("cancel-rejected-then-fill", 5));
        ctx.expect(result.status == exec::BasketStatus::Active, "撤单拒绝测试前目标单应 Active");
        result = drain_adapter_reports(engine, adapter);
        ctx.expect(result.status == exec::BasketStatus::Active, "ack 后订单应仍在途");

        adapter.push_scripted_reports({
            exec::ExecutionReport{.status = exec::OrderStatus::CancelRejected, .text = "cancel rejected"},
        });
        exec::ControlCommand cancel;
        cancel.action = exec::ControlAction::CancelBasket;
        cancel.basket_id = "cancel-rejected-then-fill";
        result = engine.control(cancel);
        ctx.expect(result.status == exec::BasketStatus::Active, "撤单拒绝前应等待通道回报");
        result = drain_adapter_reports(engine, adapter);
        ctx.expect(result.status == exec::BasketStatus::Active, "CancelRejected 后订单仍应 Active");
        ctx.expect_eq(state.effective_cash(), 50, "CancelRejected 不应释放现金预占");
        ctx.expect(state.has_working_order("A"), "CancelRejected 后订单仍应在途");

        result = engine.on_execution_report(exec::ExecutionReport{
            .order_id = 1,
            .trade_id = "T1",
            .status = exec::OrderStatus::Filled,
            .last_qty = 5,
            .cumulative_qty = 5,
            .last_price = 10,
            .text = "filled after cancel rejected",
        });
        ctx.expect(result.status == exec::BasketStatus::Complete, "撤单拒绝后再成交应正常完成");
        ctx.expect_eq(state.effective_long("A"), 5, "撤单拒绝后的成交应更新持仓");
        ctx.expect_eq(state.effective_cash(), 50, "撤单拒绝后的成交应按成交价扣现金");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(200);
        state.set_position("A", 0);
        state.set_position("B", 0);
        state.set_mark_price("A", 10);
        state.set_mark_price("B", 10);

        exec::SimAdapter adapter(state);
        adapter.push_scripted_reports({
            exec::ExecutionReport{.status = exec::OrderStatus::New, .text = "A ack"},
        });
        adapter.push_scripted_reports({
            exec::ExecutionReport{.status = exec::OrderStatus::New, .text = "B ack"},
        });
        exec::ExecutionEngine engine(state, adapter);

        exec::SetBasketTargetCommand command;
        command.basket_id = "cancel-all-after-kill";
        command.strategy_id = "test-strategy";
        command.account_id = "test-account";
        command.plan_policy.mode = exec::BasketPlanMode::Parallel;
        command.legs = {
            exec::LegTarget{.instrument_id = "A", .long_goal = exec::GoalExpression::set_quantity(5)},
            exec::LegTarget{.instrument_id = "B", .long_goal = exec::GoalExpression::set_quantity(5)},
        };

        auto result = engine.submit(command);
        ctx.expect(result.status == exec::BasketStatus::Active, "CancelAll 测试前目标单应 Active");
        result = drain_adapter_reports(engine, adapter);
        ctx.expect(result.status == exec::BasketStatus::Active, "两个 ack 后应仍等待成交");
        ctx.expect_eq(state.effective_cash(), 100, "两个买单应占用两笔现金");

        exec::ControlCommand kill;
        kill.action = exec::ControlAction::KillSwitch;
        result = engine.control(kill);
        ctx.expect(result.status == exec::BasketStatus::Complete, "KillSwitch 控制命令本身应完成");
        ctx.expect(engine.trading_state() == exec::TradingState::Killed, "KillSwitch 后状态应为 Killed");

        exec::ControlCommand cancel_all;
        cancel_all.action = exec::ControlAction::CancelAll;
        result = engine.control(cancel_all);
        ctx.expect(result.status == exec::BasketStatus::Active, "Killed 状态下 CancelAll 仍应可用");
        result = drain_adapter_reports(engine, adapter);
        ctx.expect(result.status == exec::BasketStatus::Complete, "CancelAll 回报完成后应 Complete");
        ctx.expect_eq(state.effective_cash(), 200, "CancelAll 应释放全部现金预占");
        ctx.expect(!state.has_working_order("A"), "CancelAll 后 A 不应还有在途订单");
        ctx.expect(!state.has_working_order("B"), "CancelAll 后 B 不应还有在途订单");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        RejectCancelAdapter adapter;
        exec::ExecutionEngine engine(state, adapter);

        auto result = engine.submit(make_target_command("cancel-send-failed", 5));
        ctx.expect(result.status == exec::BasketStatus::Active, "撤单发送失败测试前目标单应 Active");
        result = engine.on_execution_reports(adapter.drain_reports());
        ctx.expect(result.status == exec::BasketStatus::Active, "ack 后订单应仍在途");

        exec::ControlCommand cancel;
        cancel.action = exec::ControlAction::CancelBasket;
        cancel.basket_id = "cancel-send-failed";
        result = engine.control(cancel);
        ctx.expect(result.status == exec::BasketStatus::Failed, "找到可撤订单但 adapter 拒绝发送撤单时应 Failed");
        ctx.expect_eq(state.effective_cash(), 50, "撤单发送失败不应释放现金预占");
        ctx.expect(state.has_working_order("A"), "撤单发送失败后订单仍应在途");
    }

    return ctx.result();
}
