#pragma once

#include <deque>
#include <vector>

#include "exec/adapter/venue_adapter.hpp"
#include "exec/state/execution_state_view.hpp"

namespace exec {

class SimAdapter final : public IVenueAdapter {
public:
    explicit SimAdapter(const ExecutionStateView& state);

    void push_scripted_reports(std::vector<ExecutionReport> reports);
    bool has_pending_reports() const;
    std::vector<ExecutionReport> drain_reports();
    SendOrderResult send_order(const ChildOrder& order) override;
    SendOrderResult cancel_order(const ChildOrder& order) override;

private:
    Price default_report_price(const ChildOrder& order) const;
    void normalize_reports(const ChildOrder& order, std::vector<ExecutionReport>& reports) const;

    const ExecutionStateView& state_;
    std::deque<std::vector<ExecutionReport>> scripted_reports_;
    std::vector<ExecutionReport> pending_reports_;
};

}  // 命名空间 exec
