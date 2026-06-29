#pragma once

#include <chrono>
#include <vector>

#include "exec/core/types.hpp"
#include "exec/model/order.hpp"

namespace exec {

struct BasketActionCommand {
    BasketId basket_id;
    StrategyId strategy_id;
    AccountId account_id;
    std::vector<PositionOrderIntent> actions;
    std::uint64_t version{0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

}  // namespace exec
