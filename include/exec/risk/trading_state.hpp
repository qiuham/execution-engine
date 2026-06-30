#pragma once

#include <string>

#include "exec/model/order.hpp"

namespace exec {

enum class TradingState {
    Active,
    Reducing,
    Halted,
    Killed,
};

inline const char* to_string(TradingState state) {
    switch (state) {
        case TradingState::Active:
            return "Active";
        case TradingState::Reducing:
            return "Reducing";
        case TradingState::Halted:
            return "Halted";
        case TradingState::Killed:
            return "Killed";
    }
    return "Unknown";
}

inline bool allows_new_order(TradingState state, const PositionOrderIntent& intent) {
    if (state == TradingState::Active) {
        return true;
    }
    if (state == TradingState::Reducing) {
        return is_reducing(intent);
    }
    return false;
}

inline std::string trading_state_reject_reason(TradingState state, const PositionOrderIntent& intent) {
    if (state == TradingState::Reducing && !is_reducing(intent)) {
        return "交易状态为 Reducing，只允许降低敞口的订单";
    }
    if (state == TradingState::Halted) {
        return "交易状态为 Halted，禁止提交新订单";
    }
    if (state == TradingState::Killed) {
        return "交易状态为 Killed，禁止提交新订单，需要人工恢复";
    }
    return {};
}

}  // 命名空间 exec
