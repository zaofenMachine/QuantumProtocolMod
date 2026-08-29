# Quantum Protocol 局内检查点 Mod：第一阶段可行性报告

测试日期：2026-08-29
游戏版本环境：Unreal Engine 4.27
注入框架：UE4SS 3.0.1（官方 GitHub 发布包）

## 结论

这个 Mod **可行，但应分成“检查点触发”和“精确状态恢复”两个难度层级**。

- 在每一波/小关开始时检测检查点：高可行性。
- 检测玩家死亡并接管 Game Over：高可行性。
- 恢复生命、波次与基础牌组：中高可行性。
- 逐对象精确恢复场上卡牌、手牌顺序、弃牌、计数器、效果、行动队列和随机数状态：中等可行性，是后续主要风险。

建议先实现一个最小版本：波次开始时保存状态，死亡后重新加载战斗关卡，重建玩家牌区和生命，再调用指定波次生成函数。等该路径稳定后，再补齐卡牌动态效果与随机状态。

## 已验证事实

### 运行时注入

- 游戏可由 UE4SS 3.0.1 正常加载。
- UE4SS 正确识别引擎为 UE 4.27。
- GUObjectArray、FName、GMalloc 等核心地址扫描成功。
- 探针运行期间游戏进程保持响应，没有发生崩溃。

### 波次检查点

已找到并成功挂钩：

- `SpawnController:spawnNextWave`
- `SpawnController:spawnWaveIndex(int waveIndex)`
- `SpawnController:onWaveStart(int waveIndex)`
- `SpawnController:onFinalWaveStart(int waveIndex)`

实战中实际命中了 `spawnNextWave`，并读取到：

- `currentWaveIndex = 0`
- `lastWaveIndex = 1`
- `currentTurnCountdown = 10`
- `waveCountdownPenalty = 1`

因此存在稳定的波次开始检查点，以及可用于恢复指定波次的入口。

### 玩家生命与死亡

已找到并成功挂钩：

- `CardEngine:OnHealthChanged(int Delta, changeType)`
- `CardEngine:onLevelEnded(Type)`
- `BP_GameoverEnder:onShowGameover_Event_0`

玩家生命通过 `BottomBar.currentHealth` 和 `BottomBar.maxHealth` 可直接读取。本次测试读取到 `9/9`。

实测死亡顺序：

1. `OnHealthChanged(Delta=-1, changeType=0)`
2. `onLevelEnded(Type=1)`
3. `CardEngine.currentGameState` 变为 `3`
4. 约一秒后调用 `onShowGameover_Event_0`

这提供了两个可选恢复点：在 `onLevelEnded` 阶段阻止正常失败流程，或在 Game Over UI 阶段增加“从检查点重试”。后者侵入更小，建议先做。

### 可恢复对象边界

实战扫描确认存在以下独立控制器实例：

- `BP_ControllerBoard`
- `BP_ControllerDeck`
- `BP_ControllerHand`
- `BP_ControllerStorage`
- 两个 `BP_ControllerTrash`
- `BP_ControllerPendingCards`
- `BP_ControllerEnemyPending`
- `BP_CardEngine`
- `BP_BottomBar`

这些牌区控制器继承 `ControllerCardGroup`，其原生函数 `getCardInstanceListSorted()` 返回 `CardInfoInstance` 数组。`CardInfoInstance` 至少包含完整 `CardInfo` 和 `upgradeLevel`，说明各牌区的卡牌身份与顺序具备枚举基础。

游戏还公开了以下有用恢复函数：

- `CardEngine:resetPlayerBoard()`
- `CardEngine:clearEnemyBoard()`
- `CardEngine:clearAllActionQueue()`
- `CardEngine:loadDeck(Decklist, storage)`
- `CardEngine:healHealth(Amount)`
- `CardEngine:setNewMaxHealth(int)`
- `InGameCard:getCardInfoInstance()`
- `InGameCard:getCurrentHealth()`
- `InGameCard:getCurrentTurnCounter()`
- `InGameCard:getPlacementLocation()`

因此“清理当前战斗并按快照重建”有明确的原生 API 支撑，不必直接修改任意内存偏移。

## 主要风险

### 卡牌动态状态

仅保存 `CardInfoInstance` 不足以保证完全一致。场上卡牌可能还有：

- 当前生命、攻击和回合计数器
- 通用/特殊计数器与持续效果
- 自动化状态
- 卡牌所在槽位和牌区内顺序
- 临时生成、升级或被移出游戏的卡牌

下一阶段必须验证这些状态能否通过反射函数读取并重建。

### 行动队列和异步动画

若在动作尚未结算时保存，恢复可能产生重复动作或悬空动画。检查点应安排在波次生成完成、行动队列为空且玩家可以操作之后，而不是在 `spawnNextWave` 的前置回调里立刻落盘。

### 随机数状态

重新加载关卡可能改变抽牌或敌人生成随机结果。最小版本可以保存各牌区顺序和已选定的波次/阵型；若仍不一致，再研究 UE4 随机流或游戏自己的随机种子。

### 特殊关卡

剧情、教程、Boss 和无限模式可能覆盖 `onWaveStart`。探针已发现多个关卡专属实现，因此最终版本需要以原生 `spawnNextWave` 为兜底，并为特殊关卡设置兼容名单或禁用提示。

## 存档安全检查

死亡前已备份 `global.sav`、`slot1.sav` 和 `steam_autocloud.vdf`。

死亡后：

- `global.sav` 哈希完全不变。
- `slot1.sav` 长度不变，仅两个字节变化。
- 变化位置属于 `secondsPlayTime`，数值增加与本次测试时长一致。

本次死亡没有修改永久牌组、解锁数据或关卡完成记录。

## 第二阶段建议

1. 在战斗稳定后枚举所有牌区的 `CardInfoInstance`，保存顺序和升级等级。
2. 对场上 `InGameCard` 补充生命、回合计数器、槽位和效果快照。
3. 原型化恢复流程：暂停行动队列、清场、重载玩家牌区、恢复生命、生成保存的波次。
4. 先用手动热键触发保存/恢复，验证幂等性。
5. 稳定后再接入 Game Over UI 的“从检查点重试”。

## 证据文件

- `logs/20260829-141803/UE4SS.log`
- `logs/20260829-141803/UE4SS_ObjectDump.txt`
- `backups/save-before-death-20260829-140727/`
