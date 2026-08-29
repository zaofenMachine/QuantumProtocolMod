# CXX SDK 分析

最后更新：2026-08-30。

## 证据范围

2026-08-29 在战斗场景中通过 UE4SS 3.0.1 `GenerateSDK()` 成功生成 662 个头文件，共 3,398,221 字节。生成目录为本地逆向产物，不进入版本库。

用于本次分析的游戏可执行文件：

- 大小：82,718,720 字节。
- SHA-256：`0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6`。

关键生成文件：

| 文件 | SHA-256 |
| --- | --- |
| `Quantum.hpp` | `2CB1A019BE772322B9EEF664EDE148B10B35098F7040F99FBE17BC361DCC35A1` |
| `Quantum_enums.hpp` | `E9B96AEB12D52C3709E228BA2103BFA75C732397B9AC04CC39B277BA90BF645D` |
| `BP_CardEngine.hpp` | `2C99AD50AEAA6C3192DE7458D9EE2249DE8A72ECD996DBD41CE50E4AA0D4B0EE` |
| `BP_InGameCard.hpp` | `BA37B71899EB2BE0B5B2F5555A62953041EF6296EBE63397129963C511BD3FB0` |
| `GI_Quantum.hpp` | `E08157FFA9675D49C86C61D30CE13C592E8489D5C373A13DD4E229E52481F615` |

这些头文件可靠地列出当前已加载类型的反射属性、函数、参数以及生成器推断的布局，但不证明非反射私有状态已经完整暴露，也不保证直接写内存安全。实际 Mod 应优先按名称使用 UE4SS 反射，并把上述游戏哈希写入兼容性元数据。

## “重编程”玩家侧

### 已确认的紧凑持久结构

`FGameDeckRun` 大小为 `0x88`，包含：

- 时间戳、父牌组数据库 ID、角色标识、牌组 ID、牌组标识和牌组名称。
- `deck: TArray<FRecordableCard>`。
- `storage: TArray<FRecordableCard>`。
- `lootSets: TArray<FRecordableLootSet>`。

`FRecordableCard` 大小为 `0x28`，包含 `cardTag`、`displayText`、`upgradeLevel` 和 `count`。`FRecordableLootSet` 包含卡池名称和权重。

`UQuantumGameInstance` 同时提供：

- `getCurrentDeckRun()`。
- `getActiveDecklist()`、`getActiveDecklistInstances()` 和 `getActiveStorage()`。
- 把 `FRecordableCard` 转回 `FDecklistCard` 的转换函数。
- `setActiveDecklist()`、`setBenchCards()`、`updateActiveDecklist()` 和 `updateBench()`。

这已经足以支持“保存完整构成和升级”的玩家侧格式。读取时应把 `FGameDeckRun` 转成 Mod 自己的纯数据 JSON，而不是把 Unreal 内存结构原样写盘。

### 自动锚点候选

生成 SDK 给出了比按键轮询更可靠的事件链：

1. `ADeckbuildController::confirmDeckEditor(newDeck, newBench)`：编辑内容被确认。
2. `ABP_CardEngine_C::LoadPlayerCardsStart()`：游戏按原生重编程语义重载牌组。
3. `ACardEngine.currentGameState == EGAME_STATE::OPEN` 且动作队列稳定：允许写检查点。

正式实现可在第 1 步设置一次性的“重编程待保存”标志，在第 2 步之后等待稳定状态再保存。这样不会把普通战斗初次加载误判为重编程锚点，也不会保存编辑器尚未提交的中间状态。

### 玩家非卡牌状态

除牌组、缓存区和当前生命外，候选检查点还应只读盘点：

- `CardEngine.getTurnCount()`、当前与最大回合倒计时。
- 当前角色技能充能；读取函数为 `getCurrentCharacterCardCharge()`，恢复候选为增量函数 `addCharacterAbilityCharge()`。
- 已获得战利品 `getLoot()`、额外缓存容量和额外卡池。
- 角色、关卡和游戏模式标识。

其中哪些数值会由一次原生重编程自动保留，必须通过保存前后状态签名比较确认，不能仅凭函数名决定是否重复写回。

## 敌人精确状态

### 可以稳定读取的部分

每个 `AInGameCard` 提供：

- `getTag()`、`getId()`、`getCardInfoInstance()` 和 `getCardLocation()`。
- `getCurrentHealth()` 和 `getCurrentTurnCounter()`。
- `CardPlacementComponent`，可继续取得场上位置。
- 通用和特殊计数器对象；`AGenericCounterTracker::getCurrentCounters()` 可读当前数量。

`ASpawnController` 直接暴露生成列表、当前/最后波次索引、当前倒计时、警戒计数、自动生成开关和倒计时惩罚。`FSpawnableCard` 还明确包含卡牌标识、升级、位置和覆盖标记。

### 尚未满足“精确恢复”的部分

反射 API 没有给 `AInGameCard` 暴露权威状态 setter。当前攻击、基础攻击、最大生命、临时属性修改、动态效果集合和激活状态主要存在于非公开的原生状态中。已暴露的 `Action_*_Visuals` 函数是表现层回调，不能当作战斗逻辑写入接口。

枚举 `EQUERY_CARD_TRAITS` 证明权威状态还包括：效果数量、初始/点燃效果数量、计数器数量、属性修改器数量和活动状态。现有 getter 无法完整返回这些集合的内容。`cardOverlayEffects` 和计数器 Actor 可以作为只读交叉验证来源，但 UI 对象不能直接充当可靠存档格式。

因此，敌人精确恢复当前仍需要至少一种额外证据：

- 找到游戏用于构造 `EGAME_ACTION_TYPE::CARD_STAT_MODIFY`、`ADD_EFFECT`、`CHANGE_*_COUNTERS` 等权威动作的入口；或
- 逆向识别 `AInGameCard` 的私有状态容器并证明其可安全序列化和重新应用。

在得到该证据前，只能证明敌人“基础实例可枚举”，不能宣称可以精确恢复。

### 关卡专用状态

精确恢复范围不止敌方卡牌和基础 `SpawnController`：

- `AInfiniteSpawner` 还有当前/上一组索引、升阶等级、重投次数和 `FInfiniteSpawnerSettings`。
- `BP_Act1ReprogramSpawner` 有教程触发布尔值。
- `BP_TowerRevisitSpawner` 有最终对话触发状态。
- `BP_BossWaystoneWatcher` 有 `triggered` 状态及所跟踪卡牌引用。
- 教程 `ActionFilter` 记录已触发动作数、提示状态和特定卡牌引用。

恢复器必须按实际类枚举关卡专用 Actor 的反射状态，或明确把特殊关卡排除在首个版本之外。仅保存 `currentWaveIndex` 会在这些场景产生表面正确、后续行为错误的伪恢复。

## 跨进程重新进入战斗

`UQuantumGameInstance` 提供 `CurrentLevel`、`levelToLoad`、`activeCharacterInfo`、`activeStageInfo`、来源关卡和关卡切换类型。相关流程函数包括：

- `GI_Quantum_C::reloadBattleArea()`。
- `GI_Quantum_C::goToBattleArea(allowedCharacters, stage)`。
- `GI_Quantum_C::setBattleInformationForDebug(stage, deck, character)`。
- `ACardEngine::loadEnemyInfo(LevelInfo, spawnerIndex)`。
- `ACardEngine::loadDeck(Decklist, storage)`。

这些函数证明“重新进入指定战斗”存在可调用的原生路径，但仍需运行时验证调用顺序。`setBattleInformationForDebug` 不能直接用于成品，除非证明它不会修改原生进度或绕过正常规则。

建议恢复事务分为：读取并校验 Mod 文件、准备 `GameInstance` 上下文、触发正常关卡加载、等待新 `CardEngine` 初始化、暂停动作队列、应用状态、做状态签名校验、最后恢复输入和动作队列。任何一步失败都应放弃恢复并回到菜单。

## 对两条路线的最新影响

### 小关开头路线

优点是敌人及关卡专用辅助对象可以走原生小关生成流程。难点仍是玩家所有牌区的顺序、场上卡牌的私有权威状态、动态效果和移出游戏集合。

### 重编程路线

玩家侧已经有非常合适的 `FGameDeckRun` 表示，且原生重载语义已实测可用。难点是保存瞬间的敌人私有权威状态、关卡专用 Actor 和特殊模式控制器。

两条路线都碰到 `AInGameCard` 私有状态问题，只是对象阵营不同。“重编程”明显减少玩家牌区工作量，但特殊敌人/关卡状态使它在通用性上风险更高。当前仍不建议立即锁定最终路线。

## 下一实验门

只读 C++ 导出器已经完成首次构建，但尚未部署。部署并确认加载后，第一批运行时报告必须覆盖：

1. `FGameDeckRun`、牌组、缓存区、战利品和玩家非卡牌资源。
2. 每个敌方实例的 getter 状态、计数器、效果显示对象和位置。
3. `SpawnController` 基类与实际子类的全部反射属性。
4. 当前关卡所有非 UI、非组件的 Quantum Actor，找出会影响战斗的辅助对象。
5. 重编程确认前、确认后、重载完成后三个时间点的状态签名差异。

只有这份报告证明存档字段闭合后，才进入写回实验。
