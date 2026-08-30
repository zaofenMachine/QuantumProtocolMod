# 技术状态与已知事实

最后更新：2026-08-31。

## 环境

- 游戏：Quantum Protocol，Windows / Unreal Engine 4。
- 已测试安装目录：`F:\SteamLibrary\steamapps\common\Quantum Protocol`。
- 注入/反射工具：UE4SS 3.0.1 zDEV。
- 当前实现：UE4SS Lua 研究探针加 C++ 只读反射/原生状态导出器；尚不具备跨进程恢复。
- 正式跨进程恢复倾向 UE4SS C++ Mod。只读导出器 v0.1–v0.5 已使用 RE-UE4SS v3.0.1、UEPseudo、MSVC 14.38 和固定 Rust nightly 完成构建、部署与真实战斗验证，均未导致游戏崩溃。v0.5 复杂样本包含 141 个相关对象和 17 张活动卡。
- Windows 11 SDK `10.0.28000.0` 已被 CMake 正确选中并以 Windows `10.0.19045` 为目标完成构建，不需要额外安装 Windows 10 SDK。
- 已在战斗场景成功生成 CXX SDK：662 个头文件，关键 Quantum 类、结构和函数签名均已取得。详细证据见 [sdk-analysis.md](sdk-analysis.md)。

## 已验证能力

- C++ 模组可由 UE4SS 3.0.1 正常加载；首份 `Ctrl+F11` 报告成功写入 `Mods/QuantumCheckpoint/Reports`。游戏自身也会把 `F11` 解释为窗口模式切换，因此后续版本改用 `Ctrl+F1`。
- 第一版 C++ 报告成功读取实时生命 `9/9`、战斗状态 `OPEN`、无限模式 Spawner 的波次索引与倒计时等字段。
- 第二版通过反射调用零参数只读 getter，稳定导出完整 `GameDeckRun`、活动牌组实例、缓存区、各牌区卡表，以及单卡标签、GUID、生命、回合计数和位置。本次 9 张玩家牌全部能归入具体牌区；默认值为 0 的升级字段可能被 UE 文本导出省略。
- 第三版导出全部 20 个战场槽位，并通过对象路径把场上卡映射到阵营、前后排和索引；本次敌人 `enemyCacheRandom` 的状态为敌方前排索引 2、生命 3、回合计数 7。
- 第三版还把卡牌关联到 13 个效果显示对象及其 Widget，读取效果类型、动作状态和阻塞条件；20 个通用/特殊计数器 getter 均安全返回，本次读数全为 0。
- 第四版记录公开 getter 的原生函数地址。反汇编确认 `BP_InGameCard_C + 0x228` 指向独立的权威卡牌状态对象，并定位基础生命、当前生命、回合基础值和回合调整值。
- 第五版在两份真实战斗样本中交叉验证私有状态：普通样本 10/10、复杂样本 17/17 张卡的当前生命与回合计数都和公开 getter 完全一致。复杂样本覆盖敌我受伤、三个敌人、场上/墓地/缓存区/手牌/牌库、额外卡组编辑及跨两个小关/路线选择。
- 能定位当前战斗中的 `CardEngine`、`SpawnController`、底栏和卡牌实例。
- `Ctrl+F5` 可捕获内存快照，包括生命、波次字段、卡牌区域和场上位置。
- 场上位置可通过 `CardPlacementComponent:getPlacedFieldSlot()` 取得，已记录槽位索引、行和阵营。
- `Ctrl+F6` 通过原生 `healHealth` 把生命恢复到快照值，且受当前生命上限约束；测试中从 7 恢复到 9 成功。
- `Ctrl+F7` 可安全比较玩家各牌区与快照。
- `CardEngine:resetPlayerBoard()` 无参调用连续测试未崩溃。它会清空玩家牌库、手牌、缓存区、墓地、待处理区和场上牌，敌人及生命保持不变。
- `BP_CardEngine:LoadPlayerCardsStart()` 无参调用未崩溃。它会加载当前完整牌组、重新洗牌并抽五张；不会恢复快照时的手牌、牌序、场上或墓地。这与“重编程重开”语义吻合。

## 已确认的失败路径

### UE4SS 热重载

按 `Ctrl+R` 时发生原生访问冲突。日志表明崩溃发生在 UE4SS 卸载/注销钩子的流程附近。研究配置已将 `EnableHotReloadSystem` 设为 `0`；脚本变更后必须完全重启游戏。

### Lua 获取大型 Decklist 返回值

原 `Ctrl+F4` 直接调用 `GI_Quantum_C:getActiveDecklist()` 时发生读取地址 `0x22` 的访问冲突。推断是 UE4SS Lua 边界传递大型 `Decklist` 返回结构时的封送问题。该入口已移除；不能把此调用包在 `pcall` 中当作安全措施，因为崩溃发生在原生层。

### 当前战斗中清空并重生敌人

原 `Ctrl+F1` 调用 `clearEnemyBoard()` 后，游戏立即把当前小关判定为完成并进入下一小关，未能得到可比较的旧波次重建结果。该入口已从源码和部署副本移除。

## 已识别的关键 API

以下名称来自对象转储或实测，签名仍需由 C++ SDK/头文件继续核对：

- `CardEngine:clearAllActionQueue()`
- `CardEngine:clearEnemyBoard()`
- `CardEngine:resetPlayerBoard()`
- `CardEngine:setActionQueueSystemPaused(bool)`
- `CardEngine:drawCard()`
- `CardEngine:loadDeck(Decklist, storage)`
- `CardEngine:healHealth(int)`
- `CardEngine:playCardInstantly(FName, CardPlacement)`
- `CardEngine:removeCardsFromGame(Set<Guid>)`
- `SpawnController:spawnWaveIndex(int)`
- `SpawnController:playCardToField(FName, CardPlacement)`
- `SpawnController:makeSpawnableCardInstance(...)`
- `InGameCard:getCardInfoInstance()`
- `InGameCard:getCardLocation()`
- `InGameCard:getCurrentHealth()`
- `InGameCard:getCurrentTurnCounter()`
- `InGameCard:getId()`
- `InGameCard:getTag()`
- `ControllerCardGroup:getCardInstanceListSorted()`
- `BP_CardEngine:LoadPlayerCardsStart()`

`QuantumGameInstance` 还暴露活动牌组、缓存区和当前流程相关 getter/setter，但复杂结构不得在当前 Lua 版本中直接调用，需转入 C++ 验证。

## 已知枚举与结构线索

卡牌位置：`HAND=0`、`DECK=1`、`TRASH=2`、`FIELD=3`、`PENDING=4`、`CHARACTER=5`、`STORAGE=6`、`ENEMY_FIELD=7`、`ENEMY_PENDING=8`、`ENEMY_TRASH=9`。

场面：`PLAYER=0`、`ENEMY=1`。行：`FRONT=0`、`BACK=1`、`CORE=2`、`NONE=3`。

已发现的结构名包括 `Decklist`、`DecklistCard`、`CardInfoInstance`、`GameDeckRun`、`RecordableCard` 和 `CardPlacement`。

离线解析现有 UE4SS 对象转储后，已确认以下反射字段：

- `GameDeckRun`：`parentDeckDbId`、`characterTag`、`deckId`、`deckTag`、`deckName`、`deck`、`storage`、`lootSets` 和时间戳。
- `RecordableCard`：`cardTag`、`displayText`、`upgradeLevel`、`count`。
- `DecklistCard`：`cardName`、`count`、`upgradeLevel`。
- `CardPlacement`：位置类型、索引、行和阵营。
- `SpawnController`：波次索引、倒计时、警戒计数、生成列表以及自动生成相关字段。

其中 `GameDeckRun.deck` 和 `GameDeckRun.storage` 都是 `RecordableCard` 数组。这是“重编程”路线的重要正面证据：玩家完整牌组和缓存区的构成、升级与数量已经存在紧凑的、近似序列化用途的数据结构，不必从可视卡牌对象反推。但对象转储只证明字段可被反射发现，尚未证明跨进程写回方式或所有运行时约束。

敌人侧仍没有同等完整的单一结构。v0.3 已证明可以把 `InGameCard` 的标识、GUID、当前生命、位置、回合计数、精确槽位、效果显示和计数器关联起来，但这些状态分散在多个对象中。效果显示层是否等同于权威效果执行状态尚未证明，因此敌人精确写回依然是“重编程”路线的主要未知量。

v0.4/v0.5 进一步证明，单张活动卡的核心权威状态位于 `AInGameCard + 0x228` 指向的私有对象。当前游戏 EXE 指纹下，基础生命位于 `state + 0x118`、当前生命位于 `state + 0x11C`，回合调整量和基础值位于 `state + 0x194/+0x198`。复杂样本中的敌方 `4→1 HP`、玩家 `2→1 HP` 以及两张回合计数为 4 的敌人均交叉验证成功。详细证据和严格的版本边界见 [phase-4-native-card-state.md](phase-4-native-card-state.md)。

CXX SDK 进一步确认：已公开的卡牌状态修改函数主要是 `Action_*_Visuals` 表现层回调，不能据此写回权威战斗状态。特殊关卡还存在带独立状态的 Spawner 子类、教程过滤器和 Boss 监视器；精确恢复范围不能只限于敌方卡牌和波次索引。

## 存档安全边界

- 研究原型不读取、不覆盖游戏原生 `.sav`。
- 日志、崩溃转储、UE4SS 二进制、用户存档备份和部署运行清单不得进入版本库。
- 成品检查点应使用独立目录、格式版本、校验、大小上限和原子替换。
- 所有恢复测试应先使用可丢弃的游戏流程；没有状态签名验证前，不宣称“精确恢复”。

## 下一步需要获得的证据

1. 在可丢弃流程中执行单卡当前生命的临时写入脉冲，并在同一次调用内通过公开 getter 验证、恢复原值。
2. 在实际非零场景验证通用/特殊计数器，并收集带持续效果与特殊 Spawner 的样本。
3. 区分效果显示状态与权威效果执行状态，定位安全的读取与写回 API。
4. 把 UE `ExportTextItem` 输出转换为版本化、可校验的结构化检查点数据。
5. 验证从菜单进入指定流程/关卡的安全路径与加载时机。

具体构建要求和当前阻塞见 [cpp-development.md](cpp-development.md)。
