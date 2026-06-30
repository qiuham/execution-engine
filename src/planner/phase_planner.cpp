#include "exec/planner/phase_planner.hpp"

#include <algorithm>
#include <optional>
#include <string>

#include "exec/model/constraints.hpp"
#include "exec/planner/basket_target_collection.hpp"

namespace exec {

namespace {

bool by_priority_desc(const BasketTargetGap& lhs, const BasketTargetGap& rhs) {
    if (lhs.priority != rhs.priority) {
        return lhs.priority > rhs.priority;
    }
    return lhs.instrument_id < rhs.instrument_id;
}

std::string leg_name(const BasketTargetGap& gap) {
    return gap.instrument_id + " gap=" + std::to_string(gap.gap_long_qty);
}

bool min_notional_blocked(const BasketTargetGap& gap, Quantity quantity) {
    if (gap.constraints.min_order_notional <= 0) {
        return false;
    }
    return gap.mark_price <= 0 || estimate_notional(quantity, gap.mark_price) < gap.constraints.min_order_notional;
}

std::optional<PositionOrderIntent> build_intent(const BasketTargetGap& gap,
                                                Quantity quantity,
                                                TradeSide side) {
    if (quantity <= 0) {
        return std::nullopt;
    }

    return PositionOrderIntent{
        .instrument_id = gap.instrument_id,
        .side = side,
        .bucket = PositionBucket::Long,
        .quantity = quantity,
        .order = OrderSpec{},
        .style = gap.style,
    };
}

}  // 匿名命名空间

PlannerDecision PhasePlanner::plan_next(const BasketExecution& basket, const ExecutionStateView& state) const {
    PlannerDecision decision;
    BasketTargetCollection targets(basket, state);
    decision.notes = targets.notes();

    auto reducing = targets.reducing_gaps();
    auto increasing = targets.increasing_gaps();
    std::sort(reducing.begin(), reducing.end(), by_priority_desc);
    std::sort(increasing.begin(), increasing.end(), by_priority_desc);

    std::vector<BasketTargetGap> selected;
    if (basket.plan_policy.mode == BasketPlanMode::Parallel) {
        selected = reducing;
        selected.insert(selected.end(), increasing.begin(), increasing.end());
    } else if (basket.plan_policy.mode == BasketPlanMode::Sequential) {
        selected = reducing;
        selected.insert(selected.end(), increasing.begin(), increasing.end());
        std::sort(selected.begin(), selected.end(), by_priority_desc);
    } else if (!reducing.empty()) {
        selected = reducing;
    } else {
        selected = increasing;
    }

    auto available_cash = state.effective_cash();
    for (const auto& gap : selected) {
        auto quantity = targets.tradable_quantity(gap);
        if (quantity <= 0) {
            continue;
        }

        if (gap.gap_long_qty < 0) {
            if (min_notional_blocked(gap, quantity)) {
                decision.blocked_reasons.push_back("卖出 " + leg_name(gap) + " 缺少价格或低于最小名义金额");
                continue;
            }
            if (auto intent = build_intent(gap, quantity, TradeSide::Sell)) {
                decision.intents.push_back(*intent);
            }
        } else {
            if (gap.mark_price <= 0) {
                decision.blocked_reasons.push_back("买入 " + leg_name(gap) + " 缺少 mark price");
                continue;
            }

            const auto requested = quantity;
            const auto lot = normalized_lot_size(gap.constraints);
            const auto affordable = floor_to_lot(available_cash / gap.mark_price, lot);
            if (affordable < quantity) {
                quantity = affordable;
                decision.notes.push_back("买入 " + gap.instrument_id + " 因现金不足裁剪到 " +
                                         std::to_string(quantity));
            }

            if (quantity <= 0 || quantity < gap.constraints.min_order_qty || min_notional_blocked(gap, quantity)) {
                decision.blocked_reasons.push_back("买入 " + leg_name(gap) + " 现金不足或低于最小下单约束");
                continue;
            }

            available_cash -= estimate_notional(quantity, gap.mark_price);
            if (quantity < requested) {
                decision.notes.push_back("买入 " + gap.instrument_id + " 保留未完成余量 " +
                                         std::to_string(requested - quantity));
            }
            if (auto intent = build_intent(gap, quantity, TradeSide::Buy)) {
                decision.intents.push_back(*intent);
            }
        }

        if (basket.plan_policy.mode == BasketPlanMode::Sequential && !decision.intents.empty()) {
            break;
        }
    }

    decision.complete = decision.intents.empty() && decision.blocked_reasons.empty() &&
                        !targets.has_reducing_gap() && !targets.has_increasing_gap();
    return decision;
}

}  // 命名空间 exec
