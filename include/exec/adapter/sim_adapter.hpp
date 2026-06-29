#pragma once

#include "exec/adapter/venue_adapter.hpp"
#include "exec/state/execution_state_view.hpp"

namespace exec {

class SimAdapter final : public IVenueAdapter {
public:
    explicit SimAdapter(const ExecutionStateView& state);

    ExecutionReport send_order(const ChildOrder& order) override;

private:
    const ExecutionStateView& state_;
};

}  // namespace exec
