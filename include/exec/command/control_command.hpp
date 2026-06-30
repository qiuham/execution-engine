#pragma once

#include <chrono>
#include <optional>

#include "exec/core/types.hpp"

namespace exec {

enum class ControlAction {
    Pause,
    Resume,
    ReduceOnly,
    CancelBasket,
    CancelAll,
    KillSwitch,
};

struct ControlCommand {
    ControlAction action{ControlAction::Pause};
    AccountId account_id;
    std::optional<BasketId> basket_id{};
    std::uint64_t version{0};
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

}  // 命名空间 exec
