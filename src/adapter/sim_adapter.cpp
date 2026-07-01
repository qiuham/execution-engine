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

Price SimAdapter::default_report_price(const ChildOrder& order) const {
    auto price = order.intent.order.limit_price.value_or(
        order.intent.order.reservation_price.value_or(state_.mark_price(order.intent.instrument_id)));
    if (price <= 0) {
        price = 1;
    }
    return price;
}

void SimAdapter::normalize_reports(const ChildOrder& order, std::vector<ExecutionReport>& reports) const {
    const auto price = default_report_price(order);

    for (auto& report : reports) {
        if (report.order_id == 0) {
            report.order_id = order.order_id;
        }
        if (is_fill_status(report.status) && report.last_price <= 0) {
            report.last_price = price;
        }
    }
}

SendOrderResult SimAdapter::send_order(const ChildOrder& order) {
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
                .last_price = default_report_price(order),
                .text = "模拟立即成交",
            },
        };
    }

    normalize_reports(order, reports);

    pending_reports_.insert(pending_reports_.end(), reports.begin(), reports.end());
    return {.accepted = true, .text = "模拟通道已接收"};
}

SendOrderResult SimAdapter::cancel_order(const ChildOrder& order) {
    if (is_terminal(order.status) || open_quantity(order) <= 0) {
        return {.accepted = false, .text = "模拟通道拒绝撤单: 订单不在可撤状态"};
    }

    std::vector<ExecutionReport> reports;
    if (!scripted_reports_.empty()) {
        reports = std::move(scripted_reports_.front());
        scripted_reports_.pop_front();
    } else {
        reports = {
            ExecutionReport{
                .order_id = order.order_id,
                .status = OrderStatus::PendingCancel,
                .text = "模拟撤单已提交",
            },
            ExecutionReport{
                .order_id = order.order_id,
                .status = OrderStatus::Canceled,
                .text = "模拟撤单成功",
            },
        };
    }

    normalize_reports(order, reports);
    pending_reports_.insert(pending_reports_.end(), reports.begin(), reports.end());
    return {.accepted = true, .text = "模拟通道已接收撤单"};
}

}  // 命名空间 exec
