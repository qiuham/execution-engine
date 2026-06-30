#include "exec/adapter/sim_adapter.hpp"

#include <string>
#include <utility>

namespace exec {

SimAdapter::SimAdapter(const ExecutionStateView& state) : state_(state) {}

void SimAdapter::push_scripted_reports(std::vector<ExecutionReport> reports) {
    scripted_reports_.push_back(std::move(reports));
}

bool SimAdapter::has_pending_reports() const {
    return !pending_reports_.empty();
}

std::vector<ExecutionReport> SimAdapter::drain_reports() {
    auto reports = std::move(pending_reports_);
    pending_reports_.clear();
    return reports;
}

SendOrderResult SimAdapter::send_order(const ChildOrder& order) {
    auto price = order.intent.order.limit_price.value_or(
        order.intent.order.reservation_price.value_or(state_.mark_price(order.intent.instrument_id)));
    if (price <= 0) {
        price = 1;
    }

    std::vector<ExecutionReport> reports;
    if (!scripted_reports_.empty()) {
        reports = std::move(scripted_reports_.front());
        scripted_reports_.pop_front();
    } else {
        reports = {
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

    for (auto& report : reports) {
        if (report.order_id == 0) {
            report.order_id = order.order_id;
        }
        if (report.last_price <= 0) {
            report.last_price = price;
        }
    }

    pending_reports_.insert(pending_reports_.end(), reports.begin(), reports.end());
    return {.accepted = true, .text = "模拟通道已接收"};
}

}  // 命名空间 exec
