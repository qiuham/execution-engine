# Execution Engine

C++20 执行层原型，目标是先把以 basket 为统一入口的执行模型、订单生命周期、资源预占和模拟撮合链路跑通，再接真实交易通道。

当前版本刻意保持很小：先保证模型清楚、状态能流转、调仓能闭环，不急着做复杂规则和极低延迟优化。

## 设计方向

核心执行结构统一成：

```text
BasketExecution
  -> LegExecution
      -> PositionOrderIntent
          -> ChildOrder
```

关键约定：

- 以 basket 为统一入口：单标的执行也是一个只有一条 leg 的 basket。
- V1 先只做多：当前只真正支持 `BUY LONG` 和 `SELL LONG`。
- 内部主语义是 `BUY/SELL + LONG/SHORT + quantity`；V1 遇到 `SHORT` 目标或动作会拒绝。
- `ExecutionStateView` 不是官方持仓账本，也不是组合管理系统；它只是执行侧本地实时视图，由外部快照、成交增量、本地在途订单增量和资源预占组成。
- 规划器使用阶段式模型。默认 `ReduceThenIncrease` 表示先做降低仓位/释放资源的动作，再做增加仓位/占用资源的动作。V1 只做多时等价于先卖再买。
- `SimAdapter` 目前直接模拟立即全成交，用来先验证目标 -> 规划 -> 子单 -> 回报 -> 状态更新 的完整闭环。

## 当前执行链路

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
ExecutionStateView 资源预占
    |
    v
子单 / 订单状态机
    |
    v
SimAdapter
    |
    v
ExecutionStateView 成交增量更新
    |
    v
滚动重规划，直到 basket 完成
```

示例：

```text
初始状态：
  cash = 100
  A = 100
  B = 0

目标：
  A = 50
  B = 30

执行计划：
  SELL LONG 50 A
  BUY  LONG 30 B

最终状态：
  A = 50
  B = 30
  cash = 300
```

## 目录结构

```text
apps/execution_node/        示例可执行程序
include/exec/adapter/       交易通道接口和 SimAdapter
include/exec/command/       basket 目标/直接动作/控制命令类型
include/exec/core/          公共 ID 和基础数值类型
include/exec/engine/        ExecutionEngine 门面
include/exec/execution/     BasketExecution 和 LegExecution 状态
include/exec/model/         订单、动作、执行风格模型
include/exec/oms/           子单状态机骨架
include/exec/planner/       阶段式规划器
include/exec/state/         ExecutionStateView 和资源预占增量
src/                        实现文件
```

## 构建和运行

```bash
cmake -S . -B build
cmake --build build
./build/execution_engine
```

仓库会忽略本地 CLion/CMake 构建目录。

## V1 范围

当前已实现：

- 以 basket 为统一入口的目标命令接口。
- 绝对数量目标：`GoalOp::SetTo` + `GoalMeasure::Quantity`。
- 只做多的执行状态视图和资源预占逻辑。
- 先降低仓位、再增加仓位的阶段式规划器。
- 原型阶段的同步执行循环。
- 最小子单状态机骨架。
- 立即成交的模拟交易通道。
- 两个 leg 的调仓示例。

V1 暂不实现：

- 做空、对锁模式、翻仓/穿零策略。
- 百分比目标和名义金额目标解析。
- 真正异步事件循环。
- 真实交易所 / broker 适配器。
- 部分成交、拒单、撤单、重复回报、乱序回报等完整 OMS 边界。
- 多策略虚拟账户归因。
- 复杂 DAG / 优化器式 basket 规划器。

## 下一步计划

1. 加强 `OrderStateMachine`：覆盖 ack、reject、部分成交、全成、撤单确认、撤单拒绝、重复回报和乱序回报。
2. 扩展 `SimAdapter`：支持脚本化成交、拒单、撤单，用确定性场景测试规划器和 OMS。
3. 把资源预占逻辑从 `ExecutionStateView` 拆成独立 `ReservationBook`，并补现金和库存资源预占单测。
4. 增加 `TargetResolver`，支持名义金额和权重目标，但仍保持 V1 只做多。
5. 把同步原型循环改成事件驱动执行循环。
6. 在真实适配器前增加 `VenueMapper` 边界，让内部订单意图和交易所字段分离。
7. 增加 basket 调仓测试：先卖后买、现金不足、部分完成、单 leg 失败。

## 说明

当前优先级是正确性和执行模型清晰度，不是低延迟。等状态机、资源预占、恢复语义稳定后，再把热路径逐步改成预分配对象、环形队列和分片事件循环。
