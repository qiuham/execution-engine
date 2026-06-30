#pragma once

#include <optional>
#include <string>

#include "exec/core/types.hpp"

namespace exec {

enum class TradeSide {
    Buy,
    Sell,
};

enum class PositionBucket {
    Long,
    Short,
};

enum class OrderType {
    Market,
    Limit,
};

enum class TimeInForce {
    Gtc,
    Ioc,
    Fok,
};

enum class AlgoId {
    Immediate,
    Maker,
    Twap,
};

enum class PriceModelId {
    MarketIoc,
    PassiveTopOfBook,
    ImbalanceTopOfBook,
    External,
};

struct OrderSpec {
    OrderType type{OrderType::Market};
    TimeInForce tif{TimeInForce::Ioc};
    std::optional<Price> limit_price{};
    std::optional<Price> reservation_price{};
    bool post_only{false};
    bool reduce_only{false};
};

struct ExecutionStyle {
    AlgoId algo_id{AlgoId::Immediate};
    PriceModelId price_model_id{PriceModelId::MarketIoc};
    double aggression{1.0};
    bool allow_maker{true};
    bool allow_taker{true};
    Quantity min_child_qty{1};
    Quantity max_child_qty{0};  // V1 中 0 表示不限制。
};

struct PositionOrderIntent {
    InstrumentId instrument_id;
    TradeSide side{TradeSide::Buy};
    PositionBucket bucket{PositionBucket::Long};  // V1 只执行 Long bucket。
    Quantity quantity{0};
    OrderSpec order{};
    ExecutionStyle style{};
};

inline bool is_reducing(const PositionOrderIntent& intent) {
    return (intent.bucket == PositionBucket::Long && intent.side == TradeSide::Sell) ||
           (intent.bucket == PositionBucket::Short && intent.side == TradeSide::Buy);
}

inline const char* to_string(TradeSide side) {
    return side == TradeSide::Buy ? "BUY" : "SELL";
}

inline const char* to_string(PositionBucket bucket) {
    return bucket == PositionBucket::Long ? "LONG" : "SHORT";
}

inline const char* to_string(OrderType type) {
    return type == OrderType::Market ? "MARKET" : "LIMIT";
}

inline const char* to_string(TimeInForce tif) {
    switch (tif) {
        case TimeInForce::Gtc:
            return "GTC";
        case TimeInForce::Ioc:
            return "IOC";
        case TimeInForce::Fok:
            return "FOK";
    }
    return "UNKNOWN";
}

inline const char* to_string(PriceModelId model_id) {
    switch (model_id) {
        case PriceModelId::MarketIoc:
            return "MarketIoc";
        case PriceModelId::PassiveTopOfBook:
            return "PassiveTopOfBook";
        case PriceModelId::ImbalanceTopOfBook:
            return "ImbalanceTopOfBook";
        case PriceModelId::External:
            return "External";
    }
    return "Unknown";
}

}  // 命名空间 exec
