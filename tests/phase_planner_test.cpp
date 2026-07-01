#include "test_support.hpp"

#include "exec/planner/phase_planner.hpp"

namespace {

exec::LegExecution make_leg(const exec::InstrumentId& instrument_id,
                            exec::Quantity target_long_qty,
                            int priority = 0,
                            exec::LegConstraints constraints = {}) {
    exec::LegExecution leg;
    leg.instrument_id = instrument_id;
    leg.target_long_qty = target_long_qty;
    leg.constraints = constraints;
    leg.priority = priority;
    return leg;
}

exec::BasketExecution make_basket(exec::BasketPlanMode mode, std::vector<exec::LegExecution> legs) {
    exec::BasketExecution basket;
    basket.basket_id = "basket";
    basket.strategy_id = "strategy";
    basket.account_id = "account";
    basket.plan_policy.mode = mode;
    basket.legs = std::move(legs);
    return basket;
}

}  // 匿名命名空间

int main() {
    exec::test::TestContext ctx;
    exec::PhasePlanner planner;

    {
        exec::ExecutionStateView state;
        state.set_cash(50);
        state.set_position("A", 100);
        state.set_position("B", 0);
        state.set_mark_price("A", 10);
        state.set_mark_price("B", 10);

        const auto basket = make_basket(exec::BasketPlanMode::ReduceThenIncrease,
                                        {make_leg("A", 50), make_leg("B", 5)});
        const auto decision = planner.plan_next(basket, state);
        ctx.expect_eq(decision.intents.size(), static_cast<std::size_t>(1),
                      "ReduceThenIncrease 有卖出缺口时应只规划降低敞口");
        ctx.expect(decision.intents.front().side == exec::TradeSide::Sell,
                   "ReduceThenIncrease 第一阶段应生成卖单");
        ctx.expect_eq(decision.intents.front().instrument_id, std::string("A"),
                      "ReduceThenIncrease 应先处理卖出缺口");
        ctx.expect_eq(decision.intents.front().quantity, 50, "卖出数量应等于目标缺口");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(50);
        state.set_position("A", 100);
        state.set_position("B", 0);
        state.set_mark_price("A", 10);
        state.set_mark_price("B", 10);

        const auto basket = make_basket(exec::BasketPlanMode::Parallel, {make_leg("A", 50), make_leg("B", 5)});
        const auto decision = planner.plan_next(basket, state);
        ctx.expect_eq(decision.intents.size(), static_cast<std::size_t>(2),
                      "Parallel 应同时生成降低和增加敞口意图");
        ctx.expect(decision.intents[0].side == exec::TradeSide::Sell, "Parallel 中卖出缺口应存在");
        ctx.expect(decision.intents[1].side == exec::TradeSide::Buy, "Parallel 中买入缺口应存在");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(200);
        state.set_position("A", 0);
        state.set_position("B", 0);
        state.set_mark_price("A", 10);
        state.set_mark_price("B", 10);

        const auto basket =
            make_basket(exec::BasketPlanMode::Sequential, {make_leg("A", 5, 1), make_leg("B", 5, 9)});
        const auto decision = planner.plan_next(basket, state);
        ctx.expect_eq(decision.intents.size(), static_cast<std::size_t>(1),
                      "Sequential 每轮应只发一个最高优先级意图");
        ctx.expect_eq(decision.intents.front().instrument_id, std::string("B"),
                      "Sequential 应按 priority 降序选择 leg");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(1000);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        const auto basket = make_basket(
            exec::BasketPlanMode::ReduceThenIncrease,
            {make_leg("A", 9, 0, exec::LegConstraints{.lot_size = 5, .min_order_qty = 1})});
        const auto decision = planner.plan_next(basket, state);
        ctx.expect_eq(decision.intents.size(), static_cast<std::size_t>(1), "数量应按 lot size 向下取整");
        ctx.expect_eq(decision.intents.front().quantity, 5, "9 手目标在 lot_size=5 时应先交易 5 手");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(1000);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        const auto basket = make_basket(
            exec::BasketPlanMode::ReduceThenIncrease,
            {make_leg("A", 2, 0, exec::LegConstraints{.qty_tolerance = 2, .min_order_qty = 1})});
        const auto decision = planner.plan_next(basket, state);
        ctx.expect(decision.complete, "目标缺口在 tolerance 内时应认为完成");
        ctx.expect(decision.intents.empty(), "目标缺口在 tolerance 内时不应发单");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(1000);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        const auto basket = make_basket(
            exec::BasketPlanMode::ReduceThenIncrease,
            {make_leg("A", 4, 0, exec::LegConstraints{.min_order_qty = 5, .min_order_notional = 50})});
        const auto decision = planner.plan_next(basket, state);
        ctx.expect(decision.complete, "残差低于最小下单约束时应停止重试并视为完成");
        ctx.expect(decision.intents.empty(), "残差低于最小下单约束时不应发单");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(35);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        const auto basket = make_basket(exec::BasketPlanMode::ReduceThenIncrease, {make_leg("A", 10)});
        const auto decision = planner.plan_next(basket, state);
        ctx.expect_eq(decision.intents.size(), static_cast<std::size_t>(1), "现金不足时应尽量裁剪买入数量");
        ctx.expect_eq(decision.intents.front().quantity, 3, "35 现金、价格 10 时买入应裁剪到 3 手");
        ctx.expect(exec::test::contains_text(decision.notes, "现金不足裁剪"),
                   "现金裁剪应留下 planner 备注");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(5);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        const auto basket = make_basket(exec::BasketPlanMode::ReduceThenIncrease, {make_leg("A", 10)});
        const auto decision = planner.plan_next(basket, state);
        ctx.expect(decision.intents.empty(), "现金完全不够一手时不应发单");
        ctx.expect(!decision.blocked_reasons.empty(), "现金完全不够一手时应返回阻塞原因");
        ctx.expect(!decision.complete, "现金不足导致无法交易时不应误判完成");
    }

    {
        exec::ExecutionStateView state;
        state.set_cash(100);
        state.set_position("A", 0);
        state.set_mark_price("A", 10);

        exec::PositionOrderIntent working_buy;
        working_buy.instrument_id = "A";
        working_buy.side = exec::TradeSide::Buy;
        working_buy.bucket = exec::PositionBucket::Long;
        working_buy.quantity = 5;
        const auto reservation = state.reserve_for_submit(working_buy);
        ctx.expect(reservation.ok, "测试 projected long 前应能创建在途买单 overlay");

        const auto basket = make_basket(exec::BasketPlanMode::ReduceThenIncrease, {make_leg("A", 5)});
        const auto decision = planner.plan_next(basket, state);
        ctx.expect(decision.complete, "projected long 已达到目标时不应重复发单");
        ctx.expect(decision.intents.empty(), "projected long 已覆盖缺口时 intents 应为空");
    }

    return ctx.result();
}
