# Execution Engine

C++20 execution-layer prototype for basket-first trading execution.

The current version is intentionally small: it focuses on the core execution model, order lifecycle, reservation, and a simulated adapter before connecting to any real venue.

## Design Direction

The engine is built around one canonical execution shape:

```text
BasketExecution
  -> LegExecution
      -> PositionOrderIntent
          -> ChildOrder
```

Key choices:

- Basket-first: single-instrument execution is represented as a basket with one leg.
- V1 is long-only: supported internal actions are `BUY LONG` and `SELL LONG`.
- Internal action language is `BUY/SELL + LONG/SHORT + quantity`; V1 rejects `SHORT` actions for now.
- `ExecutionStateView` is not an official portfolio ledger. It is only a local execution view made from snapshots, fill overlays, working-order overlays, and reservations.
- Planner uses a phase model. The default `ReduceThenIncrease` mode means sell/reduce first, then buy/increase. In V1 long-only execution this is equivalent to sell first, buy later.
- `SimAdapter` fills orders immediately so that the full target -> plan -> order -> report -> state update loop can be tested without a real broker.

## Current Flow

```text
SetBasketTargetCommand
    |
    v
ExecutionEngine::resolve
    |
    v
PhasePlanner
    |
    v
ExecutionStateView reservation
    |
    v
ChildOrder / OrderStateMachine
    |
    v
SimAdapter
    |
    v
ExecutionStateView fill overlay update
    |
    v
Rolling re-plan until basket complete
```

Example demo:

```text
Initial:
  cash = 100
  A = 100
  B = 0

Target:
  A = 50
  B = 30

Plan:
  SELL LONG 50 A
  BUY  LONG 30 B

Final:
  A = 50
  B = 30
  cash = 300
```

## Repository Layout

```text
apps/execution_node/        demo executable
include/exec/adapter/       venue adapter interface and SimAdapter
include/exec/command/       basket target/action/control command types
include/exec/core/          shared IDs and primitive numeric aliases
include/exec/engine/        ExecutionEngine facade
include/exec/execution/     BasketExecution and LegExecution state
include/exec/model/         order/action/style model
include/exec/oms/           child order state machine skeleton
include/exec/planner/       phase planner
include/exec/state/         ExecutionStateView and reservation overlay
src/                        implementation files
```

## Build And Run

```bash
cmake -S . -B build
cmake --build build
./build/execution_engine
```

The existing CLion build directory is ignored by git.

## V1 Scope

Implemented now:

- Basket-first target command interface.
- Absolute quantity targets with `GoalOp::SetTo` and `GoalMeasure::Quantity`.
- Long-only state view and reservation logic.
- Reduce-then-increase phase planner.
- Synchronous execution engine loop for the prototype.
- Minimal child-order state machine.
- Simulated venue adapter with immediate fills.
- Demo for a two-leg rebalance.

Explicitly out of scope for V1:

- Short selling, hedge mode, and reversal/cross-zero policy.
- Percentage/notional target resolution.
- Real async event loop.
- Real exchange/broker adapter.
- Partial fills, rejects, cancels, duplicate reports, and out-of-order reports.
- Multi-strategy sleeve accounting.
- Complex DAG/optimizer-based basket planning.

## Next Plan

1. Strengthen `OrderStateMachine` with ack, reject, partial fill, filled, cancel ack, cancel reject, duplicate report, and out-of-order report handling.
2. Expand `SimAdapter` to script fills/rejects/cancels so planner and OMS edge cases can be tested deterministically.
3. Split reservation logic into a dedicated `ReservationBook` and add unit tests for cash and inventory reservation.
4. Add `TargetResolver` for notional and weight targets while keeping V1 long-only.
5. Convert the synchronous prototype loop into an event-driven execution loop.
6. Add a venue mapper boundary before real adapters so internal order intent stays separate from venue-specific fields.
7. Add tests for basket rebalance scenarios: sell-first, insufficient cash, partial completion, and failed leg behavior.

## Notes

This project currently optimizes for correctness and a clean execution model, not latency. Once the state machine and recovery semantics are stable, the hot path can be moved toward preallocated objects, ring buffers, and sharded event loops.
