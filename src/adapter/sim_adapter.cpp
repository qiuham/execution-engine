#include "exec/adapter/sim_adapter.hpp"

namespace exec {

SimAdapter::SimAdapter(const ExecutionStateView& state) : state_(state) {}

ExecutionReport SimAdapter::send_order(const ChildOrder& order) {
    auto price = order.intent.order.limit_price.value_or(state_.mark_price(order.intent.instrument_id));
    if (price <= 0) {
        price = 1;
    }

    return ExecutionReport{
        .order_id = order.order_id,
        .status = OrderStatus::Filled,
        .last_qty = order.intent.quantity,
        .cumulative_qty = order.intent.quantity,
        .last_price = price,
        .text = "simulated immediate fill",
    };
}

}  // namespace exec
