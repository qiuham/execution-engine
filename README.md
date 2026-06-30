# Execution Engine

C++20 执行层原型。当前目标不是做完整交易系统，而是把「上游目标 / 直接指令 -> 执行内部绝对数量 -> 子单 -> 回报 -> 执行侧状态视图」这条链路先做清楚。

## 核心边界

执行层只维护执行需要的实时视图，不维护官方持仓账本，也不做策略组合构建：

- `ExecutionStateView` = 外部快照 + 成交增量 + 在途订单 overlay + reservation。
- 上游可以给 basket 目标，也可以给直接动作；单标的是只有一个 leg 的 basket。
- 入口层可以接受数量、名义金额、权重等表达；进入 planner 后统一变成绝对目标数量和绝对子单数量。
- V1 只支持 `BUY LONG` / `SELL LONG`，遇到 short 目标或 short 动作会拒绝。
- 暂不做翻仓、借券、做空产品规则、真实撤单和真实交易通道。

## 当前执行链路

```text
ExecutionCommand
    |
    +-- SetBasketTargetCommand / BasketActionCommand / ControlCommand
    |
    v
ExecutionEngine::handle / submit / control
    |
    v
BasketTargetCollection
    |
    v
PhasePlanner
    |
    v
ExecutionStateView reservation
    |
    v
ChildOrder + OrderStateMachine
    |
    v
SimAdapter
    |
    v
ExecutionStateView 成交增量 / 未成交资源释放
    |
    v
滚动重规划，直到完成、阻塞或等待在途订单
```

默认规划模式是 `ReduceThenIncrease`，即先做降低敞口 / 释放资源的 leg，再做增加敞口 / 占用资源的 leg。只做多时就是先卖再买。

## 已实现功能

- basket-first 接口：`SetBasketTargetCommand` 支持多标的绝对数量调仓，单标的用一条 leg 兼容。
- 目标解析：`GoalOp::Keep`、`SetTo`、`ChangeBy`；`GoalMeasure::Quantity`、`Notional`、`Weight` 会在入口层解析成绝对数量。
- 直接指令：`BasketActionCommand` 可以直接提交绝对数量的 `PositionOrderIntent`。
- 目标集合：`BasketTargetCollection` 按 instrument 维护目标缺口，重复 leg 会用后出现的目标覆盖前一个目标。
- 约束和误差：每个 leg 支持 `lot_size`、`qty_tolerance`、`min_order_qty`、`min_order_notional`。
- 现金处理：买入会按当前可用现金和 mark price 裁剪到可负担数量；不可满足最小下单约束时返回 planner 阻塞原因。
- 阶段规划：支持 `ReduceThenIncrease`、`Parallel`、`Sequential`，并按 priority 排序。
- 执行状态视图：维护 long 快照、成交增量、working buy/sell、sell reservation、cash reservation、mark price。
- OMS 状态机：覆盖 ack、partial fill、filled、pending cancel、canceled、rejected、重复 trade id、overfill 截断和终态幂等忽略。
- 模拟通道：`SimAdapter` 默认返回 ack + 立即全成，也支持脚本化回报，用来测试部分成交、拒单、撤单和重复回报。
- 交易状态：`TradingState::Active`、`Reducing`、`Halted`、`Killed`；`Reducing` 只允许降低敞口，`Halted/Killed` 禁止新单。
- 同步滚动重规划：每轮成交后重新 resolve 和 plan；权重 / 名义金额目标可以基于最新执行侧视图动态换算。

## 误差处理规则

- 目标缺口在 `qty_tolerance` 内：认为已满足，不再发单。
- 缺口小于 lot 或最小数量：认为是可接受残差，不为残差反复重试。
- 下单名义金额低于 `min_order_notional`：不发单；如果这是唯一剩余缺口，则 basket 会在残差范围内完成。
- 买入现金不足：先裁剪到可负担数量；裁剪后仍不满足最小下单约束，则 planner 返回阻塞原因。
- 有在途订单时：`projected_long = effective_long + working_buy_long - working_sell_long`，避免重复发同方向订单。

## 交易状态

```text
Active    正常接收目标和子单
Reducing  只允许降低敞口的子单，例如 SELL LONG
Halted    禁止新子单，后续会保留撤单通道
Killed    kill switch 触发后的终态，需要人工恢复
```

当前 `ControlCommand` 已能切换 `Pause -> Halted`、`Resume -> Active`、`ReduceOnly -> Reducing`、`KillSwitch -> Killed`。真实撤单链路还没有接入。

## 目录结构

```text
apps/execution_node/        示例可执行程序
include/exec/adapter/       交易通道接口和 SimAdapter
include/exec/command/       basket 目标、直接动作、控制命令
include/exec/core/          公共 ID 和基础数值类型
include/exec/engine/        ExecutionEngine 门面
include/exec/execution/     BasketExecution 和 LegExecution 状态
include/exec/model/         订单、约束、执行风格模型
include/exec/oms/           子单状态机
include/exec/planner/       目标集合和阶段式规划器
include/exec/risk/          TradingState 风控闸门
include/exec/state/         执行侧状态视图和资源预占
src/                        实现文件
```

## 构建和运行

```bash
cmake -S . -B build
cmake --build build
./build/execution_engine
```

示例输出会先卖出 A，再买入 B：

```text
SELL LONG 50 A
BUY  LONG 30 B
final A=50
final B=30
final cash=300
```

## 参考过的外部实现

- QuantConnect LEAN：目标数量会按 open orders 做 projected holdings，调仓时优先处理降低保证金 / 降低仓位的目标，并按 lot size 处理残差。
- NautilusTrader：风险引擎有 `ACTIVE/REDUCING/HALTED` 状态，执行引擎对重复成交、overfill、乱序状态和 reconciliation 有明确边界。
- WonderTrader：差量执行器会维护目标和 diff，成交后更新 diff；执行单元会避免与未完成订单方向冲突，并限制单次下单数量。

这些实现的共同点是：目标解析、风险闸门、执行状态和 OMS 要分层；热路径不要混入复杂组合构建逻辑。

## 下一步计划

1. 把 `ChildOrder` 从同步局部变量升级为长期订单表，支持异步回报、撤单请求和恢复。
2. 给 `OrderStateMachine`、`PhasePlanner`、reservation、脚本化 `SimAdapter` 补单元测试。
3. 接入 cancel path：`CancelBasket`、`CancelAll`、pending cancel、cancel reject、撤单后释放资源。
4. 增加 `VenueMapper`，把内部 `BUY/SELL + LONG/SHORT + quantity` 映射到不同交易所字段。
5. 增加外部快照 reconciliation：订单、成交、持仓、资金快照和本地 overlay 的对账。
6. 性能优化：instrument 字符串换成内部整数 ID，basket/intent 预分配，热路径去日志字符串，事件循环改成无锁或少锁队列。
7. 扩展产品规则：是否可做空、最小名义金额、涨跌停、交易时段、保证金、资金费率等放到 instrument / venue metadata。

## 性能原则

当前版本为了可读性保留了 `std::string`、`std::vector` 和同步日志。后续低延迟版本会保持同样的业务边界，但把热路径改成：

- 入口解析和复杂约束在控制面完成，planner 只吃绝对数量和绝对价格。
- 每个 instrument 维护 O(1) 状态槽位，不在每个 tick 全量扫描 basket。
- 订单回报通过预分配对象和事件队列进入 OMS。
- 日志、持久化、指标上报异步化，不阻塞发单路径。
