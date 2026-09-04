# 第十阶段：复杂战斗差异与向下生命恢复

日期：2026-09-04

版本：v0.12.1-dev

状态：复杂战斗只读差异已量化；路线 C 的 5/8 向下生命恢复已修复并实机通过。场上卡、墓地、特殊资源、回合和敌人运行状态仍未精确恢复。

## 复杂基线

样本使用 `cometDungeon1 / waveIndex=2 / turn=20`，在动作结算且 CardEngine 为 `OPEN` 时手动保存。基线包括：

- 玩家生命 `5/8`；角色特殊资源 `4/6`；
- 玩家场上 3 张，其中 `naturalLemon 1/2`、`naturalApple 1/2` 受伤；
- `naturalApple` 的 `isTurnActive=False`，另外两张场上卡为 `True`，证明新 Getter 能区分已攻击状态；
- 玩家墓地 3、缓存区 2、手牌 1、牌库 2；
- 敌方场上 4 张，其中 3 张受伤；
- 两张 `enemyBug` 的当前回合计数为 1；
- 两张 `enemyInterceptor` 的触发效果状态为 `COMPLETE`。

v0.12.0 的只读清单把 `isTurnActive`、基础生命、回合基础值与调整值纳入语义签名。复杂牌区补充因活动玩家卡不再全部位于牌库/手牌而明确跳过；遗留纯净补充与新 Route C 校验和不匹配，恢复时安全忽略。

## 恢复差异

路线 C 将该检查点重入为同一 wave 的原生初始敌人和重建玩家牌区。用户观察到画面正常，但等价于“关卡刚开，同时刚执行重编程”。首次结构化差异共 12 项：

- `lastLevelChangeType NORMAL→FAST`；
- CardEngine 回合 `20→0`；
- 玩家生命 `5→8`；
- 特殊资源 `4→0`；
- 牌库、手牌、缓存和墓地分布变化；
- 玩家场上、伤势、攻击状态和位置丢失；
- 敌人数量由 4 变 5，伤势、回合计数和效果状态丢失。

未来 29 个小关计划仍逐项一致，活动玩家卡的标签/升级总集合也一致，证明现有语义底座和两个已闭合精确切片没有退化。

## 向下生命恢复失败与根因

v0.12.0 首次恢复报告为 `failed`：`Player-health native state disagrees with the reflected getters`。失败门在写入前触发，实际生命保持原生 `8/8`，没有破坏内存。

原实现把共享 PlayerState `+0x24` 当成最大生命。实时只读内存和反汇编交叉验证得到：

- `state+0x1C = 8`：当前生命；
- `state+0x20 = 8`：最大生命；
- `state+0x24 = 29`：当前最大回合倒计时。

静态调用链：

- `getCurrentHealth` thunk RVA `0x1019930` → 私有 RVA `0xE22040` → 共享状态 `+0x1C`；
- `getMaxHealth` thunk RVA `0x1019CD0` → 私有 RVA `0xE28200` → 共享状态 `+0x20`；
- `getCurrentMaxTurnCountdown` thunk RVA `0x1019960` → 私有 RVA `0xE220C0` → 共享状态 `+0x24`。

此前路线 C 只在满生命样本上通过，向下恢复一直列为发布前未测回归，因此该相邻字段错误没有被触发。

## v0.12.1 修复与验收

v0.12.1 将最大生命偏移修正为 `0x20`，并同时校验 `getCurrentHealth` 和 `getMaxHealth` 的 UFunction thunk 地址；原生值与 Getter 不一致时记录双方数值后仍拒绝写入。

最终构建：

- DLL SHA-256：`194E66E363ED64CF821C15B244E7BCCDC2B98EA1686F53A0561ABE901DD0F3AF`
- 部署 ID：`20260904-223738-374`
- 报告：`route-c-restore-20260904-145555-551.json`

从主菜单直接读取冻结的 wave 2 检查点后，画面先显示原生 `8/8`，随后恢复阶段将其更新为 `5/8`；用户确认其余表现正常。报告为 `passed`，后续清单中 CardEngine 与 BottomBar 都为 5，`healthEqual=True`，未来波次仍 `verified`。

证据目录：

- 复杂基线与首轮差异：`QuantumProtoclMod.runtime-evidence/20260904-v0120-complex-gap-baseline`
- 5/8 修复：`QuantumProtoclMod.runtime-evidence/20260904-v0121-downward-health`

## 下一步

剩余差异中，角色特殊资源 `4→0` 有公开 `CardEngine:addCharacterAbilityCharge(int)`，适合作为下一个最小精确切片。首版只处理恢复后原生值 0、目标低于需求值的正向增量，并通过 CharacterCardSlot Getter 和 UI 验证；达到需求值后可能创建能力卡，需另行测试。

玩家场上/墓地和敌人重建仍需独立的权威状态与动作队列方案，不能与特殊资源切片同时实现。
