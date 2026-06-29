#pragma once

#include <string>
#include <vector>

#include "exec/adapter/venue_adapter.hpp"
#include "exec/command/basket_target_command.hpp"
#include "exec/execution/basket_execution.hpp"
#include "exec/oms/order_state_machine.hpp"
#include "exec/planner/phase_planner.hpp"
#include "exec/state/execution_state_view.hpp"

namespace exec {

struct ExecutionResult {
    BasketStatus status{BasketStatus::Pending};
    std::vector<std::string> logs;
};

class ExecutionEngine {
public:
    ExecutionEngine(ExecutionStateView& state, IVenueAdapter& adapter);

    ExecutionResult submit(const SetBasketTargetCommand& command);

private:
    BasketExecution resolve(const SetBasketTargetCommand& command) const;
    bool validate_v1(const BasketExecution& basket, std::string& reason) const;
    void submit_child_order(const BasketExecution& basket,
                            const PositionOrderIntent& intent,
                            ExecutionResult& result);

    ExecutionStateView& state_;
    IVenueAdapter& adapter_;
    PhasePlanner planner_;
    OrderStateMachine order_state_machine_;
    ClientOrderId next_order_id_{1};
};

}  // namespace exec
