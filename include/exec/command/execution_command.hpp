#pragma once

#include <variant>

#include "exec/command/basket_action_command.hpp"
#include "exec/command/basket_target_command.hpp"
#include "exec/command/control_command.hpp"

namespace exec {

using ExecutionCommand = std::variant<SetBasketTargetCommand, BasketActionCommand, ControlCommand>;

}  // namespace exec
