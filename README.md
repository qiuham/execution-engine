# Execution Engine

C++20 执行层原型。当前目标不是做完整交易系统，而是把「上游目标 / 直接指令 -> 执行内部绝对数量 -> 子单 -> 回报 -> 执行侧状态视图」这条链路先做清楚。

## 核心边界

执行层只维护执行需要的实时视图，不维护官方持仓账本，也不做策略组合构建：

- `ExecutionStateView` = 外部快照 + 成交增量 + 在途订单 overlay + reservation。
- 上游可以给组合调仓目标，也可以给直接动作；单标的是只有一个 leg 的组合调仓。
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
ChildOrderPricer / PriceModel
    |
    v
ExecutionStateView reservation
    |
    v
内存 OrderStore 创建 ChildOrder
    |
    v
OrderStateMachine
    |
    v
VenueAdapter 发单
    |
    v
ExecutionEngine::on_execution_report
    |
    v
ExecutionStateView 成交增量 / 未成交资源释放
    |
    v
滚动重规划，直到完成、阻塞或等待在途订单
```

默认规划模式是 `ReduceThenIncrease`，即先做降低敞口 / 释放资源的 leg，再做增加敞口 / 占用资源的 leg。只做多时就是先卖再买。

## 已实现功能

- 组合调仓接口：`SetBasketTargetCommand` 支持多标的绝对数量调仓，单标的用一条 leg 兼容。
- 目标解析：`GoalOp::Keep`、`SetTo`、`ChangeBy`；`GoalMeasure::Quantity`、`Notional`、`Weight` 会在入口层解析成绝对数量。
- 直接指令：`BasketActionCommand` 可以直接提交绝对数量的 `PositionOrderIntent`。
- 目标集合：`BasketTargetCollection` 按 instrument 维护目标缺口，重复 leg 会用后出现的目标覆盖前一个目标。
- 约束和误差：每个 leg 支持 `lot_size`、`qty_tolerance`、`min_order_qty`、`min_order_notional`。
- 现金处理：买入会按当前可用现金和 mark price 裁剪到可负担数量；不可满足最小下单约束时返回 planner 阻塞原因。
- 阶段规划：支持 `ReduceThenIncrease`、`Parallel`、`Sequential`，并按 priority 排序。
- 执行状态视图：维护 long 快照、成交增量、working buy/sell、sell reservation、cash reservation、mark price、盘口一档。
- 可插拔报价：`ChildOrderPricer` 在发单前把意图转换成具体 `OrderSpec`，支持 reservation price，避免报价变化和资金预占互相打架。
- 内置报价模型：`MarketIoc`、`PassiveTopOfBook`、`ImbalanceTopOfBook`；显式 `limit_price` 会优先保留，不会被模型覆盖。
- 内存订单表：`OrderStore` 保存长期 `ChildOrder`，支持 client order id、venue order id 和调仓 id 索引，不依赖 SQLite / 数据库。
- OMS 状态机：覆盖 ack、partial fill、filled、pending cancel、canceled、rejected、重复 trade id、overfill 截断和终态幂等忽略。
- 模拟通道：`SimAdapter` 默认异步排队 ack + 立即全成回报，也支持脚本化回报，用来测试部分成交、拒单、撤单和重复回报。
- 异步回报入口：`ExecutionEngine::on_execution_report` 是唯一订单回报入口，发单函数不再同步消费成交回报。
- 终态释放：订单进入 filled / canceled / rejected 后，只释放一次未成交 reservation，避免重复回报导致重复释放。
- 异步重规划：活跃调仓的在途订单成交或撤单后，会基于原始目标再次滚动规划剩余缺口；异步拒单会释放资源并停止自动重试。
- 交易状态：`TradingState::Active`、`Reducing`、`Halted`、`Killed`；`Reducing` 只允许降低敞口，`Halted/Killed` 禁止新单。
- 事件驱动重规划：每次订单终态后重新 resolve 和 plan；权重 / 名义金额目标可以基于最新执行侧视图动态换算。
- CTest 验证：覆盖 ack-only 后异步全成、异步拒单释放资源、部分成交后撤单重规划、venue order id 反查、重复成交幂等、盘口报价模型和显式限价不被覆盖。

## 报价修正模型

报价修正放在 planner 之后、reservation 之前：

```text
绝对子单意图
    -> ChildOrderPricer
    -> 带 order_type / tif / limit_price / reservation_price 的子单
    -> reservation
    -> OrderStore / VenueAdapter
```

当前内置模型：

- `MarketIoc`：默认模型，使用 `Market + IOC`，用盘口对手价或 mark price 做资金预占。
- `PassiveTopOfBook`：买挂 best bid、卖挂 best ask，`Limit + GTC + post_only`。
- `ImbalanceTopOfBook`：盘口不平衡模型；bid 量大于 ask 量时用 ask，否则用 bid。
- `External`：预留给自定义模型；实际使用时注入自己的 `IChildOrderPricer`。

资金预占不用预测成交价裸算，而是用 `reservation_price`。报价模型可以在 `limit_price` 附近调整，但只要还在 reservation 边界内，就不需要重新跑组合规划；超过边界才应该重新报价 / 重规划。

复杂模型可以放在自定义 `IChildOrderPricer` 里，例如线性模型、树模型或小型 ONNX 推理。速度原则是：特征从内存盘口和执行状态 O(1) 读取，模型对象启动时加载，热路径不做 Python 调用、不读文件、不访问数据库、不做动态分配。

## 误差处理规则

- 目标缺口在 `qty_tolerance` 内：认为已满足，不再发单。
- 缺口小于 lot 或最小数量：认为是可接受残差，不为残差反复重试。
- 下单名义金额低于 `min_order_notional`：不发单；如果这是唯一剩余缺口，则调仓会在残差范围内完成。
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
include/exec/command/       组合目标、直接动作、控制命令
include/exec/core/          公共 ID 和基础数值类型
include/exec/engine/        ExecutionEngine 门面
include/exec/execution/     BasketExecution 和 LegExecution 状态
include/exec/market/        盘口一档和报价特征
include/exec/model/         订单、约束、执行风格模型
include/exec/oms/           内存订单表和子单状态机
include/exec/planner/       目标集合和阶段式规划器
include/exec/pricing/       子单报价和价格修正模型
include/exec/risk/          TradingState 风控闸门
include/exec/state/         执行侧状态视图和资源预占
src/                        实现文件
tests/                      异步订单流测试
```

## 构建和运行

```bash
cmake -S . -B build
cmake --build build
./build/execution_engine
ctest --test-dir build
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
- NautilusTrader：风险引擎有 `ACTIVE/REDUCING/HALTED` 状态，执行引擎对重复成交、overfill、乱序状态和 reconciliation 有明确边界；它的 OrderList / Cache / Risk / Execution 分层值得借鉴，但组合目标到先卖后买的调仓规划需要我们自己做。
- WonderTrader：差量执行器会维护目标和 diff，成交后更新 diff；执行单元会避免与未完成订单方向冲突，并限制单次下单数量。

这些实现的共同点是：目标解析、风险闸门、执行状态和 OMS 要分层；热路径不要混入复杂组合构建逻辑。

## 下一步计划

1. 接入 cancel path：`CancelBasket`、`CancelAll`、pending cancel、cancel reject、撤单后释放资源。
2. 继续补 `PhasePlanner`、reservation、脚本化 `SimAdapter`、报价模型的边界测试。
3. 增加 `VenueMapper`，把内部 `BUY/SELL + LONG/SHORT + quantity` 映射到不同交易所字段。
4. 增加外部快照 reconciliation：订单、成交、持仓、资金快照和本地 overlay 的对账。
5. 增加异步 journal / snapshot 旁路；热路径仍只写内存，持久化不能阻塞发单和回报处理。
6. 性能优化：instrument 字符串换成内部整数 ID，调仓 / intent / order 预分配，热路径去日志字符串，事件循环改成无锁或少锁队列。
7. 扩展产品规则：是否可做空、最小名义金额、涨跌停、交易时段、保证金、资金费率等放到 instrument / venue metadata。

## 性能原则

当前版本为了可读性保留了 `std::string`、`std::vector` 和同步日志。后续低延迟版本会保持同样的业务边界，但把热路径改成：

- 入口解析和复杂约束在控制面完成，planner 只吃绝对数量；pricer 输出绝对价格 / 价格约束。
- 每个 instrument 维护 O(1) 状态槽位，不在每个 tick 全量扫描调仓计划。
- 报价模型只在发子单 / 改价时运行，不在每个行情 tick 上全量跑组合规划。
- 订单回报通过预分配对象和事件队列进入 OMS。
- 日志、持久化、指标上报异步化，不阻塞发单路径。
