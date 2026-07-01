#include "test_support.hpp"

#include "exec/pricing/child_order_pricer.hpp"

namespace {

exec::PositionOrderIntent make_intent(exec::TradeSide side,
                                      exec::PriceModelId model_id = exec::PriceModelId::MarketIoc) {
    exec::PositionOrderIntent intent;
    intent.instrument_id = "A";
    intent.side = side;
    intent.bucket = exec::PositionBucket::Long;
    intent.quantity = 5;
    intent.style.price_model_id = model_id;
    return intent;
}

}  // 匿名命名空间

int main() {
    exec::test::TestContext ctx;
    exec::RuleBasedChildOrderPricer pricer;

    {
        exec::ExecutionStateView state;
        state.set_mark_price("A", 100);
        state.set_book_top("A", exec::BookTop{.bid_price = 99, .bid_qty = 10, .ask_price = 101, .ask_qty = 20});

        auto buy = pricer.price(make_intent(exec::TradeSide::Buy), state);
        ctx.expect(buy.accepted, "MarketIoc 买入应接受有效盘口");
        ctx.expect(buy.intent.order.type == exec::OrderType::Market, "MarketIoc 应输出 Market 单");
        ctx.expect(buy.intent.order.tif == exec::TimeInForce::Ioc, "MarketIoc 应输出 IOC");
        ctx.expect_eq(buy.intent.order.reservation_price.value_or(0), 101, "MarketIoc 买入应用 ask 做预占价格");

        auto sell = pricer.price(make_intent(exec::TradeSide::Sell), state);
        ctx.expect(sell.accepted, "MarketIoc 卖出应接受有效盘口");
        ctx.expect_eq(sell.intent.order.reservation_price.value_or(0), 99, "MarketIoc 卖出应用 bid 做参考价格");
    }

    {
        exec::ExecutionStateView state;
        state.set_mark_price("A", 100);

        auto passive = pricer.price(make_intent(exec::TradeSide::Buy, exec::PriceModelId::PassiveTopOfBook), state);
        ctx.expect(!passive.accepted, "PassiveTopOfBook 缺少盘口时应拒绝");

        auto imbalance =
            pricer.price(make_intent(exec::TradeSide::Buy, exec::PriceModelId::ImbalanceTopOfBook), state);
        ctx.expect(!imbalance.accepted, "ImbalanceTopOfBook 缺少盘口时应拒绝");
    }

    {
        exec::ExecutionStateView state;
        state.set_mark_price("A", 100);
        state.set_book_top("A", exec::BookTop{.bid_price = 99, .bid_qty = 10, .ask_price = 101, .ask_qty = 20});

        auto buy = pricer.price(make_intent(exec::TradeSide::Buy, exec::PriceModelId::PassiveTopOfBook), state);
        ctx.expect(buy.accepted, "PassiveTopOfBook 买入应接受有效盘口");
        ctx.expect(buy.intent.order.type == exec::OrderType::Limit, "PassiveTopOfBook 应输出 Limit 单");
        ctx.expect(buy.intent.order.tif == exec::TimeInForce::Gtc, "PassiveTopOfBook 应输出 GTC");
        ctx.expect(buy.intent.order.post_only, "PassiveTopOfBook 应设置 post_only");
        ctx.expect_eq(buy.intent.order.limit_price.value_or(0), 99, "PassiveTopOfBook 买入应挂 best bid");
        ctx.expect_eq(buy.intent.order.reservation_price.value_or(0), 101,
                      "PassiveTopOfBook 买入预占应覆盖可能的对手价");

        auto sell = pricer.price(make_intent(exec::TradeSide::Sell, exec::PriceModelId::PassiveTopOfBook), state);
        ctx.expect(sell.accepted, "PassiveTopOfBook 卖出应接受有效盘口");
        ctx.expect_eq(sell.intent.order.limit_price.value_or(0), 101, "PassiveTopOfBook 卖出应挂 best ask");
        ctx.expect_eq(sell.intent.order.reservation_price.value_or(0), 101,
                      "PassiveTopOfBook 卖出 reservation 应使用挂单价");
    }

    {
        exec::ExecutionStateView state;
        state.set_mark_price("A", 100);
        state.set_book_top("A", exec::BookTop{.bid_price = 99, .bid_qty = 100, .ask_price = 101, .ask_qty = 50});

        auto buy = pricer.price(make_intent(exec::TradeSide::Buy, exec::PriceModelId::ImbalanceTopOfBook), state);
        ctx.expect(buy.accepted, "ImbalanceTopOfBook 应接受有效盘口");
        ctx.expect_eq(buy.intent.order.limit_price.value_or(0), 101,
                      "bid 量大于 ask 量时 imbalance 模型应使用 ask");
        ctx.expect_eq(buy.intent.order.reservation_price.value_or(0), 101,
                      "imbalance 买入 reservation 应不低于对手价");
        ctx.expect(buy.features.imbalance > 0.0, "bid 量更大时 imbalance 特征应为正");
    }

    {
        exec::ExecutionStateView state;
        state.set_mark_price("A", 100);
        state.set_book_top("A", exec::BookTop{.bid_price = 99, .bid_qty = 100, .ask_price = 101, .ask_qty = 50});

        auto explicit_limit = make_intent(exec::TradeSide::Buy, exec::PriceModelId::ImbalanceTopOfBook);
        explicit_limit.order.type = exec::OrderType::Limit;
        explicit_limit.order.tif = exec::TimeInForce::Gtc;
        explicit_limit.order.limit_price = 42;
        auto priced = pricer.price(explicit_limit, state);
        ctx.expect(priced.accepted, "显式 limit_price 应直接接受");
        ctx.expect_eq(priced.intent.order.limit_price.value_or(0), 42, "显式 limit_price 不应被模型覆盖");
        ctx.expect_eq(priced.intent.order.reservation_price.value_or(0), 42,
                      "显式 limit_price 缺少 reservation 时应复用 limit_price");

        explicit_limit.order.limit_price = -1;
        priced = pricer.price(explicit_limit, state);
        ctx.expect(!priced.accepted, "显式 limit_price 非正时应拒绝");

        explicit_limit.order.limit_price = 42;
        explicit_limit.order.reservation_price = -1;
        priced = pricer.price(explicit_limit, state);
        ctx.expect(!priced.accepted, "显式 reservation_price 非正时应拒绝");
    }

    {
        exec::ExecutionStateView state;
        state.set_mark_price("A", 100);
        auto invalid = make_intent(exec::TradeSide::Buy, exec::PriceModelId::MarketIoc);
        invalid.order.type = exec::OrderType::Limit;

        const auto priced = pricer.price(invalid, state);
        ctx.expect(!priced.accepted, "Limit 类型缺少 limit_price 时不应被 MarketIoc 偷偷转成市价单");
    }

    return ctx.result();
}
