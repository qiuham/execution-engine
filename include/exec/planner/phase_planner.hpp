#pragma once

#include <string>
#include <vector>

#include "exec/execution/basket_execution.hpp"
#include "exec/model/order.hpp"
#include "exec/state/execution_state_view.hpp"

namespace exec {

struct PlannerDecision {
    std::vector<PositionOrderIntent> intents;
    std::vector<std::string> notes;
    std::vector<std::string> blocked_reasons;
    bool complete{false};
};

class PhasePlanner {
public:
    PlannerDecision plan_next(const BasketExecution& basket, const ExecutionStateView& state) const;
};

}  // 命名空间 exec
