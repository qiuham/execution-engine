#include "test_support.hpp"

#include "exec/oms/order_store.hpp"

namespace {

exec::PositionOrderIntent make_intent(exec::Quantity quantity = 10) {
    exec::PositionOrderIntent intent;
    intent.instrument_id = "A";
    intent.side = exec::TradeSide::Buy;
    intent.bucket = exec::PositionBucket::Long;
    intent.quantity = quantity;
    return intent;
}

}  // 匿名命名空间

int main() {
    exec::test::TestContext ctx;

    exec::OrderStore store;
    auto& new_order = store.create_order(1, "basket-1", make_intent());
    new_order.status = exec::OrderStatus::New;
    auto& pending_cancel = store.create_order(2, "basket-1", make_intent());
    pending_cancel.status = exec::OrderStatus::PendingCancel;
    auto& canceled = store.create_order(3, "basket-1", make_intent());
    canceled.status = exec::OrderStatus::Canceled;
    auto& partial = store.create_order(4, "basket-2", make_intent());
    partial.status = exec::OrderStatus::PartiallyFilled;
    partial.filled_qty = 6;

    store.bind_venue_order_id(1, "V1");

    ctx.expect_eq(store.size(), static_cast<std::size_t>(4), "OrderStore 应保存全部订单");
    ctx.expect(store.find_for_report(exec::ExecutionReport{.venue_order_id = "V1"}) == &new_order,
               "venue order id 应能反查本地订单");

    const auto basket_1_cancelable = store.cancelable_order_ids_for_basket("basket-1");
    ctx.expect_eq(basket_1_cancelable.size(), static_cast<std::size_t>(1),
                  "basket-1 只有 New 订单可撤");
    ctx.expect_eq(basket_1_cancelable.front(), static_cast<exec::ClientOrderId>(1),
                  "PendingCancel 和终态订单不应进入撤单候选");

    const auto all_cancelable = store.cancelable_order_ids();
    ctx.expect_eq(all_cancelable.size(), static_cast<std::size_t>(2),
                  "全局撤单候选应包含 New 和仍有剩余数量的 PartiallyFilled");
    ctx.expect(store.has_cancelable_order_for_basket("basket-1"), "basket-1 应有可撤订单");
    ctx.expect(store.has_cancelable_order_for_basket("basket-2"), "basket-2 应有可撤订单");
    ctx.expect(!store.has_cancelable_order_for_basket("missing"), "不存在的 basket 不应有可撤订单");

    return ctx.result();
}
