#include "test_support.hpp"

#include "exec/state/execution_state_view.hpp"

namespace {

exec::PositionOrderIntent make_intent(exec::TradeSide side,
                                      exec::Quantity quantity,
                                      exec::Price reservation_price = 0) {
    exec::PositionOrderIntent intent;
    intent.instrument_id = "A";
    intent.side = side;
    intent.bucket = exec::PositionBucket::Long;
    intent.quantity = quantity;
    if (reservation_price > 0) {
        intent.order.reservation_price = reservation_price;
    }
    return intent;
}

}  // 匿名命名空间

int main() {
    exec::test::TestContext ctx;

    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        const auto intent = make_intent(exec::TradeSide::Buy, 5, 12);
        auto reservation = state.reserve_for_submit(intent);
        ctx.expect(reservation.ok, "买入应能按 reservation_price 预占现金");
        ctx.expect_eq(state.effective_cash(), 40, "买入预占后可用现金应扣 reservation notional");
        ctx.expect_eq(state.working_buy_long("A"), 5, "买入预占后 working buy 应增加");
        ctx.expect_eq(state.projected_long("A"), 5, "买入预占后 projected long 应包含在途买单");

        state.apply_fill(intent, 2, 11);
        ctx.expect_eq(state.effective_long("A"), 2, "买入成交后 effective long 应增加");
        ctx.expect_eq(state.working_buy_long("A"), 3, "买入部分成交后剩余 working buy 应减少");
        ctx.expect_eq(state.effective_cash(), 42, "成交价和 reservation price 不同时现金应正确重算");

        state.release_unfilled(intent, 3);
        ctx.expect_eq(state.effective_cash(), 78, "撤掉剩余买单后应释放未成交现金预占");
        ctx.expect_eq(state.working_buy_long("A"), 0, "释放未成交买单后不应保留 working buy");
        ctx.expect(!state.has_working_order("A"), "释放全部未成交数量后不应有在途订单");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 10);
        state.set_mark_price("A", 10);

        const auto intent = make_intent(exec::TradeSide::Sell, 6);
        auto reservation = state.reserve_for_submit(intent);
        ctx.expect(reservation.ok, "卖出应能预占 long 持仓");
        ctx.expect_eq(state.projected_long("A"), 4, "卖出预占后 projected long 应扣除 working sell");
        ctx.expect_eq(state.effective_cash(), 100, "卖出预占不应提前增加现金");

        state.apply_fill(intent, 2, 9);
        ctx.expect_eq(state.effective_long("A"), 8, "卖出成交后 effective long 应减少");
        ctx.expect_eq(state.effective_cash(), 118, "卖出成交后现金应增加成交金额");
        ctx.expect_eq(state.working_sell_long("A"), 4, "卖出部分成交后剩余 working sell 应减少");

        state.release_unfilled(intent, 4);
        ctx.expect_eq(state.effective_long("A"), 8, "撤掉剩余卖单不应改变已成交持仓");
        ctx.expect_eq(state.working_sell_long("A"), 0, "释放未成交卖单后不应保留 working sell");
        ctx.expect(!state.has_working_order("A"), "释放全部卖单后不应有在途订单");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        auto reservation = state.reserve_for_submit(make_intent(exec::TradeSide::Buy, 11));
        ctx.expect(!reservation.ok, "现金不足时买入预占应失败");
        ctx.expect_eq(state.effective_cash(), 100, "买入预占失败不应改变现金");
        ctx.expect_eq(state.working_buy_long("A"), 0, "买入预占失败不应增加 working buy");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 5);
        state.set_mark_price("A", 10);

        auto reservation = state.reserve_for_submit(make_intent(exec::TradeSide::Sell, 6));
        ctx.expect(!reservation.ok, "long 持仓不足时卖出预占应失败");
        ctx.expect_eq(state.working_sell_long("A"), 0, "卖出预占失败不应增加 working sell");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 0);

        auto reservation = state.reserve_for_submit(make_intent(exec::TradeSide::Buy, 1));
        ctx.expect(!reservation.ok, "缺少 mark/limit/reservation price 时买入预占应失败");
    }

    return ctx.result();
}
