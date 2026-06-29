#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include "exec/core/types.hpp"
#include "exec/model/order.hpp"

namespace exec {

enum class GoalOp {
    Keep,
    SetTo,
    ChangeBy,
};

enum class GoalMeasure {
    Quantity,
    Notional,
    Weight,
};

struct GoalExpression {
    GoalOp op{GoalOp::Keep};
    GoalMeasure measure{GoalMeasure::Quantity};
    double value{0.0};

    static GoalExpression set_quantity(Quantity quantity) {
        return GoalExpression{GoalOp::SetTo, GoalMeasure::Quantity, static_cast<double>(quantity)};
    }
};

struct LegTarget {
    InstrumentId instrument_id;
    GoalExpression long_goal{};
    GoalExpression short_goal{};  // 为后续做空支持预留；V1 会拒绝非零 short 目标。
    std::optional<ExecutionStyle> style_override{};
    int priority{0};
};

enum class MissingLegPolicy {
    IgnoreMissing,
    TargetZero,
};

enum class BasketPlanMode {
    ReduceThenIncrease,
    Parallel,
    Sequential,
};

struct BasketPlanPolicy {
    BasketPlanMode mode{BasketPlanMode::ReduceThenIncrease};
    MissingLegPolicy missing_leg_policy{MissingLegPolicy::IgnoreMissing};
};

struct SetBasketTargetCommand {
    BasketId basket_id;
    StrategyId strategy_id;
    AccountId account_id;
    std::vector<LegTarget> legs;
    BasketPlanPolicy plan_policy{};
    ExecutionStyle default_style{};
    std::uint64_t version{0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

}  // 命名空间 exec
