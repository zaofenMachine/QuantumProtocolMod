# 技术状态与已知事实

最后更新：2026-09-04。

## 环境

- 游戏：Quantum Protocol，Windows / Unreal Engine 4。
- 已测试安装目录：`F:\SteamLibrary\steamapps\common\Quantum Protocol`。
- 注入/反射工具：UE4SS 3.0.1 zDEV。
- 当前实现：C++ v0.9.13 路线 C 核心垂直切片已闭环。v0.10.1 在不改动主 schema 2 的前提下增加独立精确 `spawnList` 补充文件；实机报告为 `passed / verified`，未来 29 个小关计划恢复前后逐项一致。手牌/牌库仍会重洗，是当前最直接的精确差异。
- 正式跨进程恢复倾向 UE4SS C++ Mod。只读导出器 v0.1–v0.5 已使用 RE-UE4SS v3.0.1、UEPseudo、MSVC 14.38 和固定 Rust nightly 完成构建、部署与真实战斗验证，均未导致游戏崩溃。v0.5 复杂样本包含 141 个相关对象和 17 张活动卡。
- Windows 11 SDK `10.0.28000.0` 已被 CMake 正确选中并以 Windows `10.0.19045` 为目标完成构建，不需要额外安装 Windows 10 SDK。
- 已在战斗场景成功生成 CXX SDK：662 个头文件，关键 Quantum 类、结构和函数签名均已取得。详细证据见 [sdk-analysis.md](sdk-analysis.md)。
- 当前已验证游戏 EXE：大小 `82718720` 字节，SHA-256 `0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6`。v0.9.x 的保存、恢复和所有私有字段诊断/写探针都使用完整散列门禁。

## 路线 C v0.9.13 实现状态

- 独立 `route-c.json` 使用 schema 2、2 MiB 上限、规范负载 FNV-1a 校验、严格字段类型、临时文件写入和原子替换；旧文件保留为 `.bak`。
- 保存只接受普通 `DUNGEON`、完整战斗对象、`OPEN`、无活动卡牌提示、普通 Spawner 尺寸及无教程/Boss 伴生对象。低频观察 CardEngine 的权威 Spawner 引用，在稳定波次变化后自动保存。
- 保存内容包括角色/关卡上下文、规范化活动牌组、缓存区、DeckRun 摘要、未消费 `lootDrops`、波次、生命和 Spawner 身份。空 UE 数组采用可安全回读的 `()` 表示。
- 恢复先校验 schema、校验和、EXE 指纹和内容门禁，再调用 `reloadBattleArea()`。新 CardEngine BeginPlay 前重放角色与启动牌组，新 SpawnController BeginPlay 前再次重放运行态、暂时关闭 `autoSpawn` 并设置目标前一波次，让原生延迟启动进入目标波次。
- 玩家初始化稳定后逐项恢复新掉落物和生命；随后验证波次、生命/上限、规范化牌组、缓存和 `lootDrops`，恢复 `autoSpawn` 并经过 3 秒稳定窗口才写 `passed`。总超时为 90 秒。
- 一次恢复通过后，同一进程再次读取相同负载会复用最近一次已验证检查点作为回滚基线，避免旧 `DeckRun` 状态中的 `getActiveDecklist()` 卡死。活动事务期间重复热键会被互斥门拒绝。
- 特殊资源诊断已纳入只读导出与恢复报告：最终样本恢复前和连续三轮恢复后均为 `characterCardCharge=0`、需求 `6`、`isCharacterAbilityOk=True`，且 UI 正常。
- `lootDrops` 最终样本为香蕉、苹果、火球术三张；三轮 Getter 都精确匹配，第三轮在重编程界面人工确认显示正常。
- 持久化测试覆盖 UTF-8/转义、篡改拒绝、模式与 Spawner 门、schema 迁移、`lootDrops` 往返及复杂 UE 数组拆分；构建脚本自动运行 CTest，当前 `1/1` 通过。
- 最终 DLL SHA-256 为 `601C841D4879A1CC48E7311F7C691A890C09C908870079091666F20F717F9B94`，部署 ID `20260903-011758-412`。最终三轮报告均为 `passed`，详见 [phase-7-route-c-vertical-slice.md](phase-7-route-c-vertical-slice.md)。
- 仍属扩大覆盖面的回归项：目标生命低于/高于原生初始值、更多普通 Spawner，以及标题/完全退出入口的最终构建矩阵。它们不再阻塞精确恢复的只读差异探索，但在发布为通用成品前必须补齐。

## 已验证能力

- `scripts/Compare-BattleInventories.ps1` 可把两份 `Ctrl+F1` 清单归一化为结构化差异，忽略地址、对象序号、函数指针和预期重建的 GUID，同时比较关卡上下文、引擎状态、牌区、单卡状态、槽位、生命、回合、计数器、效果、特殊资源及未来波次计划。
- 未加精确层的同波次样本中，当前敌人/引擎/生命/特殊资源相同，但玩家手牌与牌库不同，且 29 项 `spawnList` 中有 6 项换序。v0.10.1 把 `spawnList` 存入与 Route C 校验和绑定的独立 schema 1 补充文件，在新 Spawner 基础设施就绪后导入并双阶段读回验证；最终样本 29/29 项一致。
- v0.10.0 尝试在 SpawnController BeginPlay 后置回调应用时发现属性尚不可用，安全报告 `failed-no-write` 且路线 C 继续通过。v0.10.1 把应用时点后移约 50 ms 后成功，证明精确层的失败降级边界有效。详见 [phase-8-exact-state-gap.md](phase-8-exact-state-gap.md)。

- C++ 模组可由 UE4SS 3.0.1 正常加载；首份 `Ctrl+F11` 报告成功写入 `Mods/QuantumCheckpoint/Reports`。游戏自身也会把 `F11` 解释为窗口模式切换，因此后续版本改用 `Ctrl+F1`。
- 第一版 C++ 报告成功读取实时生命 `9/9`、战斗状态 `OPEN`、无限模式 Spawner 的波次索引与倒计时等字段。
- 第二版通过反射调用零参数只读 getter，稳定导出完整 `GameDeckRun`、活动牌组实例、缓存区、各牌区卡表，以及单卡标签、GUID、生命、回合计数和位置。本次 9 张玩家牌全部能归入具体牌区；默认值为 0 的升级字段可能被 UE 文本导出省略。
- 第三版导出全部 20 个战场槽位，并通过对象路径把场上卡映射到阵营、前后排和索引；本次敌人 `enemyCacheRandom` 的状态为敌方前排索引 2、生命 3、回合计数 7。
- 第三版还把卡牌关联到 13 个效果显示对象及其 Widget，读取效果类型、动作状态和阻塞条件；20 个通用/特殊计数器 getter 均安全返回，本次读数全为 0。
- 第四版记录公开 getter 的原生函数地址。反汇编确认 `BP_InGameCard_C + 0x228` 指向独立的权威卡牌状态对象，并定位基础生命、当前生命、回合基础值和回合调整值。
- 第五版在两份真实战斗样本中交叉验证私有状态：普通样本 10/10、复杂样本 17/17 张卡的当前生命与回合计数都和公开 getter 完全一致。复杂样本覆盖敌我受伤、三个敌人、场上/墓地/缓存区/手牌/牌库、额外卡组编辑及跨两个小关/路线选择。
- v0.6.1 首次通过游戏私有 setter 临时修改权威当前生命：目标从 1 临时变为 2，直接读回为 2，随后恢复为 1；恢复后的公开 getter 也为 1。无崩溃、无卡住、无持续 UI 变化。v0.6 曾因不必要的 `getCardLocation()` 卡在写入前，未执行任何 setter；该 getter 已从安全关键路径移除。
- v0.7 把同一受保护写入跨帧保持 `1003 ms`。恢复前重新确认卡牌对象、状态指针和临时值均未变化，随后成功恢复 `2→1`；游戏无崩溃，生命 UI 可见地先增加再恢复，控制台与结构化报告均正常。这证明权威生命写入可跨帧持久并驱动 UI 同步。
- v0.8 对回合调整量执行同等门禁的跨帧写入，基础值保持 `0`，公开计数和 UI 均按 `5→6→5` 变化；实际保持 `1004 ms`，恢复前对象身份、状态指针、基础值和临时调整量全部复核通过。生命和回合计数现已具备独立验证过的精确写回基础。
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

### 路线 C v0.9.0 首轮读取 abort

2026-09-01 首轮实机覆盖了三种入口：战斗内 Esc 菜单读取、返回标题后读取、重启后在主界面直接读取，三次均退出。Windows WER 分别在 `21:40`、`21:44`、`21:45` 记录相同的 `BEX64`：`ucrtbase.dll + 0x7286e`、异常 `0xc0000409`、FAST_FAIL 数据 `7`；该偏移位于 `abort()` 内。游戏未生成新的 Crash Reporter 转储，预期目录也没有 `route-c.json` 或恢复报告，因此界面位置不是分歧点，且不能把这些样本计为进入恢复状态机后的失败。

代码审查发现复杂零参数 Getter 使用了仅清零、未按 FProperty 初始化的返回缓冲区；自动/手动保存读取 `Decklist`、Storage 和 DeckRun 时可能破坏返回对象生命周期。v0.9.1 改为统一的 `ReflectedParameterBuffer` 初始化/逆序析构；无存档读取改用 `error_code` 路径并给出显式拒绝；整个 `on_update` 增加最后异常边界。`route-c-trace.log` 对热键接收、读取、复杂 Getter、写盘、导入和重载阶段使用 `FILE_FLAG_WRITE_THROUGH` 与 `FlushFileBuffers`。这些是针对现有证据的修复候选，必须由下一轮实机结果确认，尚不能宣称根因已闭合。

### v0.9.1 普通地牢 Spawner 识别失败

无存档 F6 已在两个独立游戏进程中安全拒绝，轨迹完整到 `manual-load.refused`，且没有新增 WER 崩溃。随后保存测试中，实机只读报告确认 CardEngine 为 `OPEN`、BottomBar/GameInstance/敌人与 13 张活动卡均存在，关卡为 `sourceLevelName=neskaraDungeon1`、`Type=DUNGEON`；缺失项只有按名称查找的 `Spawner_C`。`activeStageInfo` 显示权威生成器实际为原生类 `/Script/Quantum.NeskaraDungeon1Spawner`，不含蓝图 `_C` 后缀。

v0.9.2 不再依赖生成器名称：从活动 CardEngine 的反射对象字段 `mEnemySpawnController` 取得精确实例，验证其继承 `/Script/Quantum.SpawnController` 且暴露 `currentWaveIndex/spawnWaveIndex`，并同样用 `mHUDBottomBar` 锚定 HUD。名称枚举只保留为诊断回退；每次捕获会分别记录四个必需对象是否存在。

### v0.9.2 空缓存区与自动锚点误判

v0.9.2 第三小关手动保存已找到 GameInstance、CardEngine、原生 SpawnController 和 BottomBar，且读取活动牌组成功；随后 `getActiveStorage()` 返回空文本，旧门禁将其当成 Getter 缺失而拒绝。第三阶段只读报告早已证明 UE 4.27 的空 `TArray` 返回值会导出为空文本，因此这是合法空缓存区，不是读取失败。v0.9.3 仅在返回属性确认为 `FArrayProperty` 时把空文本规范化为可回读的 `()`。

同一进程的控制台和强制刷盘轨迹没有任何 Mod 自动捕获记录；第一小关看到的自动保存是游戏自身提示。旧 C++ 自动捕获只监听 `spawnWaveIndex(int)`，但第一阶段实机已确认正常小关推进命中的是 `spawnNextWave()`。v0.9.3 在 `spawnNextWave` 后读取权威 `currentWaveIndex` 并延迟 1.5 秒捕获，同时保留 `spawnWaveIndex` 用于恢复重定向；自动拒绝原因也会写入持久轨迹。

### v0.9.3 ProcessEvent 层未命中原生波次调用

v0.9.3 在新进程中确认反射初始化成功，`spawnWaveIndex`、`spawnNextWave` 和 SpawnController 类全部找到，但进入第一小关后没有任何自动调度轨迹，也没有检查点。这表明游戏的原生波次调用不经过当前注册的全局 `UObject::ProcessEvent` 回调；仅增加函数名不会解决。

v0.9.4 改用 `UObjectGlobals::RegisterHook(UFunction*)`，与已实机命中的 Lua `RegisterHook` 共用 UE4SS 原生 UFunction 入口。`spawnWaveIndex` 前置回调保留参数重定向，`spawnNextWave` 后置回调读取权威 `currentWaveIndex` 并调度捕获；钩子 ID 会保存并在模块析构时注销。只有原生恢复钩子注册成功时 F6 才可进入重载事务。

v0.9.4 新进程在第一小关手动保存成功，生成了首个完整 `route-c.json`：普通 `cometDungeon1`、原生 `CometDungeon1Spawner`、波次 `0`、生命 `9/9`，活动牌组、合法空缓存 `()` 和 DeckRun 均写入并通过校验。进入第二小关后文件未自动覆盖，轨迹也没有任何原生回调调度记录，因此具体 UFunction 钩子同样不是正常推进的可靠自动保存入口。

v0.9.5 每 750 ms 只筛选一次活动 `BP_CardEngine_C`，从其 `mEnemySpawnController` 权威引用读取 `currentWaveIndex`；仅在战斗为 `OPEN` 且 Spawner 实例、World 或波次变化时调度一次 1.5 秒延迟捕获。它不猜测事件函数，也不在每帧执行完整检查点捕获。原生 `spawnWaveIndex` 前置钩子仍仅用于检验恢复重载是否经过可重定向入口。

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

1. 定位玩家手牌/牌库的权威容器或定向原生移动入口；公开 `ControllerCardGroup` 目前只有读取 Getter，`Action_MoveCard_Resolve_Visuals` 只是表现层，不能直接用作恢复实现。
2. 在任何牌区写入前，先建立只读所有权/偏移验证及单张卡自动回滚探针；首个目标只处理第一小关的手牌/牌库分配，不同时扩展到场上、墓地和效果。
3. 单独确认恢复后保留的 `lastLevelChangeType=FAST` 是否会影响后续玩法；若只是已消费的加载方式标记，则从精确玩法签名中降为诊断差异。
4. 作为发布前回归矩阵补测非满生命的向上/向下恢复、更多普通 Spawner，以及标题与完全重启入口。

具体构建要求见 [cpp-development.md](cpp-development.md)。
