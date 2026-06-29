#pragma once

#include <vector>

#include "exec/execution/basket_execution.hpp"
#include "exec/model/order.hpp"
#include "exec/state/execution_state_view.hpp"

namespace exec {

class PhasePlanner {
public:
    std::vector<PositionOrderIntent> plan_next(const BasketExecution& basket,
                                               const ExecutionStateView& state) const;
};

}  // 命名空间 exec
