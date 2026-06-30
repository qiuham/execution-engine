#pragma once

#include <string>
#include <vector>

#include "exec/execution/basket_execution.hpp"
#include "exec/model/constraints.hpp"
#include "exec/state/execution_state_view.hpp"

namespace exec {

struct BasketTargetGap {
    InstrumentId instrument_id;
    Quantity target_long_qty{0};
    Quantity projected_long_qty{0};
    Quantity gap_long_qty{0};
    Price mark_price{0};
    LegConstraints constraints{};
    ExecutionStyle style{};
    int priority{0};
};

class BasketTargetCollection {
public:
    BasketTargetCollection(const BasketExecution& basket, const ExecutionStateView& state);

    const std::vector<BasketTargetGap>& legs() const;
    const std::vector<std::string>& notes() const;

    std::vector<BasketTargetGap> reducing_gaps() const;
    std::vector<BasketTargetGap> increasing_gaps() const;

    bool has_reducing_gap() const;
    bool has_increasing_gap() const;

    Quantity tradable_quantity(const BasketTargetGap& gap) const;

private:
    void add_or_replace(const BasketTargetGap& gap);

    std::vector<BasketTargetGap> legs_;
    std::vector<std::string> notes_;
};

}  // 命名空间 exec
