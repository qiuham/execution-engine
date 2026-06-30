#include "exec/state/execution_state_view.hpp"

namespace exec {

namespace {
Notional notional_for(Quantity qty, Price price) {
    return static_cast<Notional>(qty) * static_cast<Notional>(price);
}
}  // 匿名命名空间

void ExecutionStateView::set_cash(Notional cash) {
    snapshot_cash_ = cash;
}

void ExecutionStateView::set_position(const InstrumentId& instrument_id, Quantity long_qty) {
    mutable_instrument(instrument_id).snapshot_long = long_qty;
}

void ExecutionStateView::set_mark_price(const InstrumentId& instrument_id, Price mark_price_value) {
    mutable_instrument(instrument_id).mark_price = mark_price_value;
}

Quantity ExecutionStateView::effective_long(const InstrumentId& instrument_id) const {
    const auto* state = find_instrument(instrument_id);
    if (state == nullptr) {
        return 0;
    }
    return state->snapshot_long + state->fill_delta_long;
}

Quantity ExecutionStateView::projected_long(const InstrumentId& instrument_id) const {
    const auto* state = find_instrument(instrument_id);
    if (state == nullptr) {
        return 0;
    }
    return state->snapshot_long + state->fill_delta_long + state->working_buy_long - state->working_sell_long;
}

Quantity ExecutionStateView::working_buy_long(const InstrumentId& instrument_id) const {
    const auto* state = find_instrument(instrument_id);
    if (state == nullptr) {
        return 0;
    }
    return state->working_buy_long;
}

Quantity ExecutionStateView::working_sell_long(const InstrumentId& instrument_id) const {
    const auto* state = find_instrument(instrument_id);
    if (state == nullptr) {
        return 0;
    }
    return state->working_sell_long;
}

bool ExecutionStateView::has_working_order(const InstrumentId& instrument_id) const {
    const auto* state = find_instrument(instrument_id);
    if (state == nullptr) {
        return false;
    }
    return state->working_buy_long > 0 || state->working_sell_long > 0;
}

Notional ExecutionStateView::effective_cash() const {
    return snapshot_cash_ + cash_delta_ - reserved_cash_;
}

Notional ExecutionStateView::total_equity() const {
    Notional equity = snapshot_cash_ + cash_delta_;
    for (const auto& [instrument_id, state] : instruments_) {
        (void)instrument_id;
        if (state.mark_price > 0) {
            equity += notional_for(state.snapshot_long + state.fill_delta_long, state.mark_price);
        }
    }
    return equity;
}

Price ExecutionStateView::mark_price(const InstrumentId& instrument_id) const {
    const auto* state = find_instrument(instrument_id);
    if (state == nullptr) {
        return 0;
    }
    return state->mark_price;
}

ReservationResult ExecutionStateView::reserve_for_submit(const PositionOrderIntent& intent) {
    if (intent.quantity <= 0) {
        return {false, "数量必须为正"};
    }
    if (intent.bucket != PositionBucket::Long) {
        return {false, "V1 只支持 LONG bucket"};
    }

    auto& state = mutable_instrument(intent.instrument_id);
    if (intent.side == TradeSide::Sell) {
        const auto available = effective_long(intent.instrument_id) - state.reserved_sell_long;
        if (available < intent.quantity) {
            return {false, "long 持仓不足，无法卖出"};
        }
        state.reserved_sell_long += intent.quantity;
        state.working_sell_long += intent.quantity;
        return {true, {}};
    }

    const auto price = state.mark_price;
    if (price <= 0) {
        return {false, "买入 reservation 缺少 mark price"};
    }
    const auto cost = notional_for(intent.quantity, price);
    if (effective_cash() < cost) {
        return {false, "现金不足，无法买入"};
    }
    reserved_cash_ += cost;
    state.working_buy_long += intent.quantity;
    return {true, {}};
}

void ExecutionStateView::apply_fill(const PositionOrderIntent& intent, Quantity fill_qty, Price fill_price) {
    if (fill_qty <= 0 || intent.bucket != PositionBucket::Long) {
        return;
    }

    auto& state = mutable_instrument(intent.instrument_id);
    const auto mark = state.mark_price > 0 ? state.mark_price : fill_price;
    const auto reserved_notional = notional_for(fill_qty, mark);
    const auto fill_notional = notional_for(fill_qty, fill_price);

    if (intent.side == TradeSide::Buy) {
        state.working_buy_long -= fill_qty;
        if (state.working_buy_long < 0) {
            state.working_buy_long = 0;
        }
        reserved_cash_ -= reserved_notional;
        if (reserved_cash_ < 0) {
            reserved_cash_ = 0;
        }
        state.fill_delta_long += fill_qty;
        cash_delta_ -= fill_notional;
        return;
    }

    state.working_sell_long -= fill_qty;
    if (state.working_sell_long < 0) {
        state.working_sell_long = 0;
    }
    state.reserved_sell_long -= fill_qty;
    if (state.reserved_sell_long < 0) {
        state.reserved_sell_long = 0;
    }
    state.fill_delta_long -= fill_qty;
    cash_delta_ += fill_notional;
}

void ExecutionStateView::release_unfilled(const PositionOrderIntent& intent, Quantity unfilled_qty) {
    if (unfilled_qty <= 0) {
        return;
    }

    auto& state = mutable_instrument(intent.instrument_id);
    if (intent.bucket != PositionBucket::Long) {
        return;
    }

    if (intent.side == TradeSide::Sell) {
        state.working_sell_long -= unfilled_qty;
        if (state.working_sell_long < 0) {
            state.working_sell_long = 0;
        }
        state.reserved_sell_long -= unfilled_qty;
        if (state.reserved_sell_long < 0) {
            state.reserved_sell_long = 0;
        }
        return;
    }

    state.working_buy_long -= unfilled_qty;
    if (state.working_buy_long < 0) {
        state.working_buy_long = 0;
    }
    const auto price = state.mark_price;
    if (price > 0) {
        reserved_cash_ -= notional_for(unfilled_qty, price);
        if (reserved_cash_ < 0) {
            reserved_cash_ = 0;
        }
    }
}

void ExecutionStateView::release_rejected(const PositionOrderIntent& intent) {
    release_unfilled(intent, intent.quantity);
}

InstrumentExecutionState& ExecutionStateView::mutable_instrument(const InstrumentId& instrument_id) {
    return instruments_[instrument_id];
}

const InstrumentExecutionState* ExecutionStateView::find_instrument(const InstrumentId& instrument_id) const {
    const auto it = instruments_.find(instrument_id);
    if (it == instruments_.end()) {
        return nullptr;
    }
    return &it->second;
}

}  // 命名空间 exec
