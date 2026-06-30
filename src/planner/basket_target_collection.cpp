#include "exec/planner/basket_target_collection.hpp"

#include <algorithm>

namespace exec {

BasketTargetCollection::BasketTargetCollection(const BasketExecution& basket, const ExecutionStateView& state) {
    legs_.reserve(basket.legs.size());
    for (const auto& leg : basket.legs) {
        BasketTargetGap gap;
        gap.instrument_id = leg.instrument_id;
        gap.target_long_qty = leg.target_long_qty;
        gap.projected_long_qty = state.projected_long(leg.instrument_id);
        gap.gap_long_qty = gap.target_long_qty - gap.projected_long_qty;
        gap.mark_price = state.mark_price(leg.instrument_id);
        gap.constraints = leg.constraints;
        gap.style = leg.style;
        gap.priority = leg.priority;
        add_or_replace(gap);
    }
}

const std::vector<BasketTargetGap>& BasketTargetCollection::legs() const {
    return legs_;
}

const std::vector<std::string>& BasketTargetCollection::notes() const {
    return notes_;
}

std::vector<BasketTargetGap> BasketTargetCollection::reducing_gaps() const {
    std::vector<BasketTargetGap> result;
    for (const auto& gap : legs_) {
        if (gap.gap_long_qty < 0 && tradable_quantity(gap) > 0) {
            result.push_back(gap);
        }
    }
    return result;
}

std::vector<BasketTargetGap> BasketTargetCollection::increasing_gaps() const {
    std::vector<BasketTargetGap> result;
    for (const auto& gap : legs_) {
        if (gap.gap_long_qty > 0 && tradable_quantity(gap) > 0) {
            result.push_back(gap);
        }
    }
    return result;
}

bool BasketTargetCollection::has_reducing_gap() const {
    return std::any_of(legs_.begin(), legs_.end(), [this](const BasketTargetGap& gap) {
        return gap.gap_long_qty < 0 && tradable_quantity(gap) > 0;
    });
}

bool BasketTargetCollection::has_increasing_gap() const {
    return std::any_of(legs_.begin(), legs_.end(), [this](const BasketTargetGap& gap) {
        return gap.gap_long_qty > 0 && tradable_quantity(gap) > 0;
    });
}

Quantity BasketTargetCollection::tradable_quantity(const BasketTargetGap& gap) const {
    if (within_qty_tolerance(gap.gap_long_qty, gap.constraints)) {
        return 0;
    }

    auto quantity = floor_to_lot(abs_qty(gap.gap_long_qty), normalized_lot_size(gap.constraints));
    if (quantity <= 0 || quantity < gap.constraints.min_order_qty) {
        return 0;
    }

    if (gap.constraints.min_order_notional > 0) {
        if (gap.mark_price <= 0) {
            return quantity;
        }
        const auto notional = estimate_notional(quantity, gap.mark_price);
        if (notional <= 0 || notional < gap.constraints.min_order_notional) {
            return 0;
        }
    }

    return quantity;
}

void BasketTargetCollection::add_or_replace(const BasketTargetGap& gap) {
    auto it = std::find_if(legs_.begin(), legs_.end(), [&gap](const BasketTargetGap& existing) {
        return existing.instrument_id == gap.instrument_id;
    });
    if (it == legs_.end()) {
        legs_.push_back(gap);
        return;
    }

    notes_.push_back("重复 leg " + gap.instrument_id + "，使用后出现的目标覆盖前一个目标");
    *it = gap;
}

}  // 命名空间 exec
