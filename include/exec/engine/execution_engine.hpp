#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "exec/adapter/venue_adapter.hpp"
#include "exec/command/basket_action_command.hpp"
#include "exec/command/basket_target_command.hpp"
#include "exec/command/control_command.hpp"
#include "exec/command/execution_command.hpp"
#include "exec/execution/basket_execution.hpp"
#include "exec/oms/order_store.hpp"
#include "exec/oms/order_state_machine.hpp"
#include "exec/planner/phase_planner.hpp"
#include "exec/pricing/child_order_pricer.hpp"
#include "exec/risk/trading_state.hpp"
#include "exec/state/execution_state_view.hpp"

namespace exec {

struct ExecutionResult {
    BasketStatus status{BasketStatus::Pending};
    std::vector<std::string> logs;
};

class ExecutionEngine {
public:
    ExecutionEngine(ExecutionStateView& state, IVenueAdapter& adapter);
    ExecutionEngine(ExecutionStateView& state,
                    IVenueAdapter& adapter,
                    std::shared_ptr<IChildOrderPricer> pricer);

    ExecutionResult submit(const SetBasketTargetCommand& command);
    ExecutionResult submit(const BasketActionCommand& command);
    ExecutionResult control(const ControlCommand& command);
    ExecutionResult handle(const ExecutionCommand& command);
    ExecutionResult on_execution_report(const ExecutionReport& report);
    ExecutionResult on_execution_reports(const std::vector<ExecutionReport>& reports);

    void set_trading_state(TradingState state);
    TradingState trading_state() const;

private:
    BasketExecution resolve(const SetBasketTargetCommand& command) const;
    Quantity resolve_goal_quantity(const GoalExpression& goal,
                                   Quantity current_qty,
                                   Price mark_price,
                                   Notional total_equity) const;
    bool validate_v1(const BasketExecution& basket, std::string& reason) const;
    bool has_working_orders(const BasketExecution& basket) const;
    bool submit_child_order(const BasketExecution& basket,
                            const PositionOrderIntent& intent,
                            ExecutionResult& result);
    void apply_order_report(ChildOrder& order,
                            const ExecutionReport& report,
                            ExecutionResult& result);
    void release_terminal_order(ChildOrder& order, ExecutionResult& result);

    ExecutionStateView& state_;
    IVenueAdapter& adapter_;
    std::shared_ptr<IChildOrderPricer> pricer_;
    PhasePlanner planner_;
    OrderStateMachine order_state_machine_;
    OrderStore order_store_;
    std::unordered_map<BasketId, SetBasketTargetCommand> active_target_commands_;
    ClientOrderId next_order_id_{1};
    TradingState trading_state_{TradingState::Active};
};

}  // 命名空间 exec
