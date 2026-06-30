#pragma once

#include <vector>

#include "exec/oms/order_state_machine.hpp"

namespace exec {

class IVenueAdapter {
public:
    virtual ~IVenueAdapter() = default;

    virtual std::vector<ExecutionReport> send_order(const ChildOrder& order) = 0;
};

}  // 命名空间 exec
