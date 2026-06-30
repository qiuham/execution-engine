#pragma once

#include <string>
#include <unordered_map>

#include "exec/core/types.hpp"
#include "exec/market/market_data.hpp"
#include "exec/model/order.hpp"

namespace exec {

struct InstrumentExecutionState {
    Quantity snapshot_long{0};
    Quantity fill_delta_long{0};
    Quantity working_buy_long{0};
    Quantity working_sell_long{0};
    Quantity reserved_sell_long{0};
    Price mark_price{0};
    BookTop book_top{};
};

struct ReservationResult {
    bool ok{true};
    std::string reason;
};

class ExecutionStateView {
public:
    void set_cash(Notional cash);
    void set_position(const InstrumentId& instrument_id, Quantity long_qty);
    void set_mark_price(const InstrumentId& instrument_id, Price mark_price);
    void set_book_top(const InstrumentId& instrument_id, BookTop book_top);

    Quantity effective_long(const InstrumentId& instrument_id) const;
    Quantity projected_long(const InstrumentId& instrument_id) const;
    Quantity working_buy_long(const InstrumentId& instrument_id) const;
    Quantity working_sell_long(const InstrumentId& instrument_id) const;
    bool has_working_order(const InstrumentId& instrument_id) const;
    Notional effective_cash() const;
    Notional total_equity() const;
    Price mark_price(const InstrumentId& instrument_id) const;
    BookTop book_top(const InstrumentId& instrument_id) const;

    ReservationResult reserve_for_submit(const PositionOrderIntent& intent);
    void apply_fill(const PositionOrderIntent& intent, Quantity fill_qty, Price fill_price);
    void release_unfilled(const PositionOrderIntent& intent, Quantity unfilled_qty);

private:
    InstrumentExecutionState& mutable_instrument(const InstrumentId& instrument_id);
    const InstrumentExecutionState* find_instrument(const InstrumentId& instrument_id) const;

    std::unordered_map<InstrumentId, InstrumentExecutionState> instruments_;
    Notional snapshot_cash_{0};
    Notional cash_delta_{0};
    Notional reserved_cash_{0};
};

}  // 命名空间 exec
