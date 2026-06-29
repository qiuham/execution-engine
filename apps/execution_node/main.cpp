#include <iostream>

#include "exec/adapter/sim_adapter.hpp"
#include "exec/command/basket_target_command.hpp"
#include "exec/engine/execution_engine.hpp"
#include "exec/state/execution_state_view.hpp"

int main() {
    exec::ExecutionStateView state;
    state.set_cash(100);
    state.set_position("A", 100);
    state.set_position("B", 0);
    state.set_mark_price("A", 10);
    state.set_mark_price("B", 10);

    exec::SimAdapter adapter(state);
    exec::ExecutionEngine engine(state, adapter);

    exec::SetBasketTargetCommand command;
    command.basket_id = "rebalance-001";
    command.strategy_id = "demo-strategy";
    command.account_id = "demo-account";
    command.plan_policy.mode = exec::BasketPlanMode::ReduceThenIncrease;
    command.legs = {
        exec::LegTarget{.instrument_id = "A", .long_goal = exec::GoalExpression::set_quantity(50)},
        exec::LegTarget{.instrument_id = "B", .long_goal = exec::GoalExpression::set_quantity(30)},
    };

    const auto result = engine.submit(command);
    for (const auto& line : result.logs) {
        std::cout << line << '\n';
    }

    std::cout << "final A=" << state.effective_long("A") << '\n';
    std::cout << "final B=" << state.effective_long("B") << '\n';
    std::cout << "final cash=" << state.effective_cash() << '\n';
    return result.status == exec::BasketStatus::Complete ? 0 : 1;
}
