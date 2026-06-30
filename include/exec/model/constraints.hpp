#pragma once

#include "exec/core/types.hpp"

namespace exec {

struct LegConstraints {
    Quantity lot_size{1};
    Quantity qty_tolerance{0};
    Quantity min_order_qty{1};
    Notional min_order_notional{0};
};

inline Quantity abs_qty(Quantity value) {
    return value < 0 ? -value : value;
}

inline Quantity normalized_lot_size(const LegConstraints& constraints) {
    return constraints.lot_size > 0 ? constraints.lot_size : 1;
}

inline Quantity floor_to_lot(Quantity quantity, Quantity lot_size) {
    if (quantity <= 0) {
        return 0;
    }
    const auto lot = lot_size > 0 ? lot_size : 1;
    return (quantity / lot) * lot;
}

inline bool within_qty_tolerance(Quantity gap, const LegConstraints& constraints) {
    return abs_qty(gap) <= constraints.qty_tolerance;
}

inline Notional estimate_notional(Quantity quantity, Price price) {
    if (quantity <= 0 || price <= 0) {
        return 0;
    }
    return static_cast<Notional>(quantity) * static_cast<Notional>(price);
}

}  // 命名空间 exec
