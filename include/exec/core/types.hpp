#pragma once

#include <cstdint>
#include <string>

namespace exec {

using AccountId = std::string;
using BasketId = std::string;
using ClientOrderId = std::uint64_t;
using InstrumentId = std::string;
using StrategyId = std::string;

// V1 先使用整数手数和整数报价单位；接入具体交易通道后再替换为定点数封装。
using Quantity = std::int64_t;
using Price = std::int64_t;
using Notional = std::int64_t;

inline constexpr Quantity kZeroQty = 0;
inline constexpr Price kZeroPrice = 0;
inline constexpr Notional kZeroNotional = 0;

}  // 命名空间 exec
