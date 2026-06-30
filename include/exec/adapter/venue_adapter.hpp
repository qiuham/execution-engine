#pragma once

#include <string>

#include "exec/oms/order_state_machine.hpp"

namespace exec {

struct SendOrderResult {
    bool accepted{true};
    std::string text;
};

class IVenueAdapter {
public:
    virtual ~IVenueAdapter() = default;

    virtual SendOrderResult send_order(const ChildOrder& order) = 0;
};

}  // 命名空间 exec
