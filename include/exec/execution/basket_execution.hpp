#pragma once

#include <string>
#include <vector>

#include "exec/command/basket_target_command.hpp"
#include "exec/core/types.hpp"
#include "exec/model/order.hpp"

namespace exec {

enum class BasketStatus {
    Pending,
    Active,
    Complete,
    Rejected,
    Failed,
};

enum class LegStatus {
    Pending,
    Active,
    Complete,
    Rejected,
    Failed,
};

struct LegExecution {
    InstrumentId instrument_id;
    Quantity target_long_qty{0};
    Quantity target_short_qty{0};
    LegStatus status{LegStatus::Pending};
    ExecutionStyle style{};
    int priority{0};
};

struct BasketExecution {
    BasketId basket_id;
    StrategyId strategy_id;
    AccountId account_id;
    BasketPlanPolicy plan_policy{};
    ExecutionStyle default_style{};
    BasketStatus status{BasketStatus::Pending};
    std::vector<LegExecution> legs;
    std::string reject_reason;
};

}  // 命名空间 exec
