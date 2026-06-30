#include "exec/adapter/sim_adapter.hpp"

#include <string>
#include <utility>

namespace exec {

SimAdapter::SimAdapter(const ExecutionStateView& state) : state_(state) {}

void SimAdapter::push_scripted_reports(std::vector<ExecutionReport> reports) {
    scripted_reports_.push_back(std::move(reports));
}

std::vector<ExecutionReport> SimAdapter::send_order(const ChildOrder& order) {
    auto price = order.intent.order.limit_price.value_or(state_.mark_price(order.intent.instrument_id));
    if (price <= 0) {
        price = 1;
    }

    if (!scripted_reports_.empty()) {
        auto reports = std::move(scripted_reports_.front());
        scripted_reports_.pop_front();
        for (auto& report : reports) {
            if (report.order_id == 0) {
                report.order_id = order.order_id;
            }
            if (report.last_price <= 0) {
                report.last_price = price;
            }
        }
        return reports;
    }

    return {
        ExecutionReport{
            .order_id = order.order_id,
            .status = OrderStatus::New,
            .text = "模拟 ack",
        },
        ExecutionReport{
            .order_id = order.order_id,
            .trade_id = "sim-" + std::to_string(order.order_id) + "-1",
            .status = OrderStatus::Filled,
            .last_qty = order.intent.quantity,
            .cumulative_qty = order.intent.quantity,
            .last_price = price,
            .text = "模拟立即成交",
        },
    };
}

}  // 命名空间 exec
