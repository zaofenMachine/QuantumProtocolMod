# 第八阶段：精确状态差异与未来小关计划

日期：2026-09-04

版本：v0.10.1-dev

状态：首个可选精确切片已闭环；未来 29 个小关的 `spawnList` 在恢复前后逐项一致，路线 C 仍正常通过。

## 基线差异

在 `cometDungeon1 / waveIndex=0 / 9/9 HP` 的同一检查点上，分别于路线 C 恢复前后按 `Ctrl+F1` 导出只读清单。`scripts/Compare-BattleInventories.ps1` 会忽略对象地址、对象序号和函数指针，按关卡状态、牌区、卡牌语义、精确槽位、生命、回合、计数器、效果及 Spawner 计划进行比较。

未加精确层的首份同波次样本得到：

- CardEngine 状态、玩家生命、特殊资源、Spawner 当前运行标量及当前敌人状态全部一致；
- 9 张玩家卡的标签、升级和总数量一致，但手牌/牌库的分配不同；
- 12 张活动卡的 GUID 全部重建，符合新战斗上下文的预期；
- 29 个 `spawnList` 项中有 6 个索引换序，说明重载重新生成了未来小关计划；
- `lastLevelChangeType` 从 `NORMAL` 变为恢复流程留下的 `FAST`。

因此路线 C 的当前波次语义已经稳定，但“未来会遇到哪些小关、以什么顺序出现”也属于可观察运行状态，不能只比较当前敌人。

## 首个最小写回切片

选择 `SpawnController.spawnList`，理由是：

- CXX SDK 明确给出 `TArray<FSpawnableCardFormation> spawnList`，其中包含每个未来小关的卡牌、升级、槽位与倒计时；
- 该字段可通过现有 FProperty 文本路径完整导出、导入并立即全文读回；
- 它不需要创建或销毁活动卡牌，也不依赖未验证的私有牌区容器；
- 当前波次由路线 C 原生生成，精确层只需在受控波次启动前换入保存的计划；
- 写入失败时可以把新 Spawner 自己生成的原计划原样导回。

精确数据没有加入稳定的 `route-c.json` schema 2，而是存入独立的 `route-c-exact-spawn-plan.json` schema 1。补充文件记录 Route C 负载校验和、EXE 指纹、关卡、波次、Spawner 类/尺寸和自己的负载校验和。缺失、损坏、过期或不匹配的补充文件只会被忽略，不会使路线 C 检查点失效。

## 时序修正

v0.10.0 首轮安全降级为 `failed-no-write`：SpawnController BeginPlay 后置回调发生时 `spawnList` 尚未暴露，没有执行任何写入，路线 C 仍为 `passed`，画面正常。

v0.10.1 把应用点移到约 50 ms 后的 `AwaitingBattleInfrastructure` 轮询。此时新 Spawner 已可导出完整 `spawnList`，而受控目标波次尚未启动。流程为：

1. 读取并严格验证与 Route C 绑定的精确补充文件。
2. 保存新 Spawner 自己生成的 `spawnList` 作为回滚材料。
3. 导入检查点中的 `spawnList` 并立即全文读回。
4. 继续路线 C 的受控波次启动。
5. 波次稳定后及三秒稳定窗口内再次比较；不一致则恢复原计划。
6. 报告独立记录 `verified`、`failed-no-write`、`failed-rolled-back` 或 `unavailable`，不把普通精确层缺失升级为路线 C 失败。

## v0.10.1 实机结果

最终 DLL SHA-256 为 `C1FFFE7943228226A1FCEE77ED06A34F194098F5EAFE58006310234CF5DFE858`，部署 ID 为 `20260904-015002-348`。

恢复报告 `route-c-restore-20260903-175416-696.json` 为 `passed`，精确状态为 `verified`。轨迹依次出现：

```text
restore.exact-spawn-plan.loaded
restore.exact-spawn-plan.import.begin
restore.exact-spawn-plan.import.complete
restore.exact-spawn-plan.verified
```

归一化对照结果：

| 范围 | 结果 |
| --- | --- |
| CardEngine、生命和底栏 | 一致 |
| 角色特殊资源 | 一致，`0/6 / True` |
| Spawner 当前波次、倒计时及警戒标量 | 一致 |
| 未来 `spawnList` | 一致，29/29 项，差异索引为空 |
| 当前敌人、槽位、生命、回合、计数器和效果 | 一致 |
| 玩家卡标签/升级总集合 | 一致 |
| 玩家手牌/牌库 | 不一致，仍会重洗牌 |
| `lastLevelChangeType` | `NORMAL → FAST` |
| 运行时卡牌 GUID | 0/12 共用，预期的新实例身份 |

用户确认恢复后画面正常。运行证据保存在仓库外的 `QuantumProtoclMod.runtime-evidence/20260904-v0101-exact-spawn-plan`。

## 当前边界与下一步

项目仍不宣称完整精确恢复。当前剩余的直接差异是玩家牌区和 `lastLevelChangeType`；更复杂的中途保存还会涉及玩家场上/墓地、敌人伤势、回合、计数器、效果与临时修改。

下一切片不应直接写 `InGameCard` 的表现层回调。先定位玩家牌区的权威容器或能定向移动卡牌的原生动作入口；若只能通过未知私有布局修改，应先做只读偏移/所有权验证和自动回滚探针。`lastLevelChangeType` 可以单独评估，但在证明它影响恢复后的玩法前，优先级低于可见的手牌/牌库偏差。
