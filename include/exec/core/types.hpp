#pragma once

#include <cstdint>
#include <string>

namespace exec {

using AccountId = std::string;
using BasketId = std::string;
using ClientOrderId = std::uint64_t;
using InstrumentId = std::string;
using StrategyId = std::string;

// V1 uses integer lots and integer quote units. Replace with fixed-point wrappers
// once venue-specific tick/lot scaling is introduced.
using Quantity = std::int64_t;
using Price = std::int64_t;
using Notional = std::int64_t;

inline constexpr Quantity kZeroQty = 0;
inline constexpr Price kZeroPrice = 0;
inline constexpr Notional kZeroNotional = 0;

}  // namespace exec
