#include "test_support.hpp"

#include "exec/oms/order_state_machine.hpp"

namespace {

exec::PositionOrderIntent make_intent(exec::Quantity quantity = 10) {
    exec::PositionOrderIntent intent;
    intent.instrument_id = "A";
    intent.side = exec::TradeSide::Buy;
    intent.bucket = exec::PositionBucket::Long;
    intent.quantity = quantity;
    return intent;
}

exec::ChildOrder make_order(exec::ClientOrderId order_id = 1, exec::Quantity quantity = 10) {
    exec::ChildOrder order;
    order.order_id = order_id;
    order.basket_id = "basket";
    order.intent = make_intent(quantity);
    return order;
}

void apply_ack(exec::OrderStateMachine& machine, exec::ChildOrder& order) {
    (void)machine.apply(order, exec::ExecutionReport{
                                   .order_id = order.order_id,
                                   .status = exec::OrderStatus::New,
                                   .text = "ack",
                               });
}

}  // 匿名命名空间

int main() {
    exec::test::TestContext ctx;
    exec::OrderStateMachine machine;

    {
        auto order = make_order();
        auto transition = machine.apply(order, exec::ExecutionReport{
                                                   .order_id = order.order_id,
                                                   .status = exec::OrderStatus::New,
                                                   .text = "ack",
                                               });
        ctx.expect(transition.accepted, "PendingNew -> New 应被接受");
        ctx.expect(order.status == exec::OrderStatus::New, "ack 后订单状态应为 New");

        transition = machine.apply(order, exec::ExecutionReport{
                                              .order_id = order.order_id,
                                              .trade_id = "T1",
                                              .status = exec::OrderStatus::PartiallyFilled,
                                              .last_qty = 4,
                                              .cumulative_qty = 4,
                                              .last_price = 10,
                                              .text = "partial",
                                          });
        ctx.expect_eq(transition.fill_qty, 4, "部分成交增量应为 4");
        ctx.expect(order.status == exec::OrderStatus::PartiallyFilled, "部分成交后状态应为 PartiallyFilled");

        transition = machine.apply(order, exec::ExecutionReport{
                                              .order_id = order.order_id,
                                              .trade_id = "T2",
                                              .status = exec::OrderStatus::Filled,
                                              .cumulative_qty = 10,
                                              .last_price = 11,
                                              .text = "filled",
                                          });
        ctx.expect_eq(transition.fill_qty, 6, "最终成交应只补剩余 6");
        ctx.expect(transition.terminal, "全成回报应进入终态");
        ctx.expect(order.status == exec::OrderStatus::Filled, "最终状态应为 Filled");

        transition = machine.apply(order, exec::ExecutionReport{
                                              .order_id = order.order_id,
                                              .trade_id = "T3",
                                              .status = exec::OrderStatus::Filled,
                                              .last_qty = 10,
                                              .last_price = 12,
                                              .text = "late fill",
                                          });
        ctx.expect(!transition.accepted, "终态后的迟到成交应幂等忽略");
        ctx.expect_eq(order.filled_qty, 10, "终态后的迟到成交不应重复增加 filled_qty");
    }

    {
        auto order = make_order();
        apply_ack(machine, order);
        auto transition = machine.apply(order, exec::ExecutionReport{
                                                   .order_id = order.order_id,
                                                   .trade_id = "T4",
                                                   .status = exec::OrderStatus::PartiallyFilled,
                                                   .last_qty = 3,
                                                   .cumulative_qty = 3,
                                                   .last_price = 10,
                                               });
        ctx.expect_eq(transition.fill_qty, 3, "撤单前部分成交应被接受");

        transition = machine.apply(order, exec::ExecutionReport{
                                              .order_id = order.order_id,
                                              .status = exec::OrderStatus::PendingCancel,
                                              .text = "pending cancel",
                                          });
        ctx.expect(transition.accepted, "PendingCancel 回报应被接受");
        ctx.expect(order.status == exec::OrderStatus::PendingCancel, "撤单请求后状态应为 PendingCancel");
        ctx.expect(!exec::is_cancelable(order), "PendingCancel 订单不应再次进入撤单候选");

        transition = machine.apply(order, exec::ExecutionReport{
                                              .order_id = order.order_id,
                                              .status = exec::OrderStatus::CancelRejected,
                                              .text = "cancel rejected",
                                          });
        ctx.expect(order.status == exec::OrderStatus::PartiallyFilled,
                   "部分成交订单撤单拒绝后应回到 PartiallyFilled");
        ctx.expect(!transition.terminal, "CancelRejected 不是终态");
        ctx.expect(exec::is_cancelable(order), "CancelRejected 后订单应重新可撤");

        transition = machine.apply(order, exec::ExecutionReport{
                                              .order_id = order.order_id,
                                              .status = exec::OrderStatus::PendingCancel,
                                              .text = "pending cancel again",
                                          });
        ctx.expect(transition.accepted, "再次 PendingCancel 应被接受");
        transition = machine.apply(order, exec::ExecutionReport{
                                              .order_id = order.order_id,
                                              .status = exec::OrderStatus::Canceled,
                                              .text = "canceled",
                                          });
        ctx.expect(transition.terminal, "Canceled 应进入终态");
        ctx.expect(order.status == exec::OrderStatus::Canceled, "撤单成功后状态应为 Canceled");
    }

    {
        auto order = make_order();
        apply_ack(machine, order);
        auto transition = machine.apply(order, exec::ExecutionReport{
                                                   .order_id = order.order_id,
                                                   .trade_id = "T5",
                                                   .status = exec::OrderStatus::PartiallyFilled,
                                                   .last_qty = 4,
                                                   .cumulative_qty = 4,
                                                   .last_price = 10,
                                               });
        ctx.expect_eq(transition.fill_qty, 4, "首次 trade id 应产生成交增量");
        transition = machine.apply(order, exec::ExecutionReport{
                                              .order_id = order.order_id,
                                              .trade_id = "T5",
                                              .status = exec::OrderStatus::PartiallyFilled,
                                              .last_qty = 4,
                                              .cumulative_qty = 8,
                                              .last_price = 10,
                                          });
        ctx.expect(!transition.accepted, "重复 trade id 应被忽略");
        ctx.expect_eq(order.filled_qty, 4, "重复 trade id 不应重复累计成交");
    }

    {
        auto order = make_order();
        apply_ack(machine, order);
        auto transition = machine.apply(order, exec::ExecutionReport{
                                                   .order_id = order.order_id,
                                                   .trade_id = "T6",
                                                   .status = exec::OrderStatus::Filled,
                                                   .last_qty = 20,
                                                   .last_price = 10,
                                               });
        ctx.expect_eq(transition.fill_qty, 10, "overfill 应按剩余数量截断");
        ctx.expect_eq(order.filled_qty, 10, "overfill 后 filled_qty 不应超过订单数量");
        ctx.expect(order.status == exec::OrderStatus::Filled, "overfill 截断后订单应完成");
    }

    {
        auto order = make_order();
        auto transition = machine.apply(order, exec::ExecutionReport{
                                                   .order_id = 99,
                                                   .status = exec::OrderStatus::New,
                                                   .text = "wrong order",
                                               });
        ctx.expect(!transition.accepted, "order_id 不匹配的回报应忽略");
        ctx.expect(order.status == exec::OrderStatus::PendingNew, "order_id 不匹配不应改变订单状态");
    }

    {
        auto order = make_order();
        apply_ack(machine, order);
        auto transition = machine.apply(order, exec::ExecutionReport{
                                                   .order_id = order.order_id,
                                                   .status = exec::OrderStatus::Filled,
                                                   .text = "missing qty",
                                               });
        ctx.expect(!transition.accepted, "缺少数量的 Filled 回报不应被接受");
        ctx.expect(order.status == exec::OrderStatus::New, "缺少数量的 Filled 回报不应把订单推入终态");
        ctx.expect_eq(order.filled_qty, 0, "缺少数量的 Filled 回报不应改变成交数量");
    }

    return ctx.result();
}
