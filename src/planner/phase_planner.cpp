#include "exec/planner/phase_planner.hpp"

#include <algorithm>
#include <cstdlib>

namespace exec {

std::vector<PositionOrderIntent> PhasePlanner::plan_next(const BasketExecution& basket,
                                                         const ExecutionStateView& state) const {
    std::vector<PositionOrderIntent> reducing;
    std::vector<PositionOrderIntent> increasing;

    for (const auto& leg : basket.legs) {
        const auto projected = state.projected_long(leg.instrument_id);
        const auto gap = leg.target_long_qty - projected;
        if (gap < 0) {
            reducing.push_back(PositionOrderIntent{
                .instrument_id = leg.instrument_id,
                .side = TradeSide::Sell,
                .bucket = PositionBucket::Long,
                .quantity = -gap,
                .order = OrderSpec{},
                .style = leg.style,
            });
        } else if (gap > 0) {
            increasing.push_back(PositionOrderIntent{
                .instrument_id = leg.instrument_id,
                .side = TradeSide::Buy,
                .bucket = PositionBucket::Long,
                .quantity = gap,
                .order = OrderSpec{},
                .style = leg.style,
            });
        }
    }

    if (basket.plan_policy.mode == BasketPlanMode::Parallel) {
        reducing.insert(reducing.end(), increasing.begin(), increasing.end());
        return reducing;
    }

    if (basket.plan_policy.mode == BasketPlanMode::Sequential) {
        auto by_priority = [&basket](const PositionOrderIntent& lhs, const PositionOrderIntent& rhs) {
            auto priority_of = [&basket](const InstrumentId& instrument_id) {
                for (const auto& leg : basket.legs) {
                    if (leg.instrument_id == instrument_id) {
                        return leg.priority;
                    }
                }
                return 0;
            };
            return priority_of(lhs.instrument_id) > priority_of(rhs.instrument_id);
        };
        reducing.insert(reducing.end(), increasing.begin(), increasing.end());
        std::sort(reducing.begin(), reducing.end(), by_priority);
        if (!reducing.empty()) {
            reducing.resize(1);
        }
        return reducing;
    }

    if (!reducing.empty()) {
        return reducing;
    }
    return increasing;
}

}  // 命名空间 exec
