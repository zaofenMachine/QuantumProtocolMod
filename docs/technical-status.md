# 技术状态与已知事实

最后更新：2026-09-01。

## 环境

- 游戏：Quantum Protocol，Windows / Unreal Engine 4。
- 已测试安装目录：`F:\SteamLibrary\steamapps\common\Quantum Protocol`。
- 注入/反射工具：UE4SS 3.0.1 zDEV。
- 当前实现：C++ v0.9.5 路线 C 修正版已部署。v0.9.0 首轮实机读取三次触发相同运行库 `abort`；v0.9.1 修正复杂反射返回值生命周期和读取边界；v0.9.2 修正原生 SpawnController 子类识别；v0.9.3 修正合法空缓存区；v0.9.4 的具体原生 UFunction 钩子在正常小关切换中仍未命中，但手动保存首次完整成功；v0.9.5 改以低频读取权威 `currentWaveIndex` 驱动自动保存。构建与离线测试通过，自动保存和恢复真实运行验收尚未完成。
- 正式跨进程恢复倾向 UE4SS C++ Mod。只读导出器 v0.1–v0.5 已使用 RE-UE4SS v3.0.1、UEPseudo、MSVC 14.38 和固定 Rust nightly 完成构建、部署与真实战斗验证，均未导致游戏崩溃。v0.5 复杂样本包含 141 个相关对象和 17 张活动卡。
- Windows 11 SDK `10.0.28000.0` 已被 CMake 正确选中并以 Windows `10.0.19045` 为目标完成构建，不需要额外安装 Windows 10 SDK。
- 已在战斗场景成功生成 CXX SDK：662 个头文件，关键 Quantum 类、结构和函数签名均已取得。详细证据见 [sdk-analysis.md](sdk-analysis.md)。
- 当前已验证游戏 EXE：大小 `82718720` 字节，SHA-256 `0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6`。v0.9.x 的保存、恢复和所有私有字段诊断/写探针都使用完整散列门禁。

## 路线 C v0.9.5 实现状态

- 已实现独立 `route-c.json`，schema 1、2 MiB 上限、规范负载 FNV-1a 校验、严格字段类型检查、临时文件写入和原子替换；旧文件保留为 `.bak`。
- 已加入持久化单元测试，覆盖 UTF-8/转义往返、负载篡改拒绝、无限模式拒绝和有状态 Spawner 拒绝；构建脚本现在自动运行 CTest，当前 `1/1` 通过。
- 保存只接受普通 `DUNGEON`、完整战斗对象、`OPEN` 状态、无活动卡牌提示、普通 Spawner 尺寸及无已知教程/Boss 伴生对象。
- 恢复通过 `QuantumGameInstance.reloadBattleArea()` 重入战斗，并用具体原生 UFunction 前置钩子重定向首次 `SpawnController.spawnWaveIndex(int)`。
- 新战斗稳定后暂停动作队列，原生清空并重载玩家牌区、恢复生命，再验证 Spawner 实例/类型、波次、生命、活动牌组和缓存区；45 秒超时与所有异常均写结构化报告。
- 重载前任一关卡/牌组导入失败会尽力回滚原 GameInstance 数据；失败收尾只在仍是同一个活动 CardEngine 时恢复动作队列，避免解引用跨关卡旧对象。
- 向下恢复生命不再只修改 BottomBar。v0.9 在完整 EXE 指纹、CardEngine getter thunk RVA、类尺寸、内存页权限及 getter/私有值交叉验证全部通过后，写入 CardEngine 权威生命状态，再同步 BottomBar 并发送零增量 UI 通知；任一验证失败会尽力回滚原生命。
- 恢复在波次拦截时记录新 Spawner 所属 World，后续只从该 World 选择 CardEngine、Spawner 和 BottomBar，并跳过已销毁或不可达 UObject，避免关卡切换期间误选旧实例。
- v0.9.5 当前 DLL 为 `643072` 字节，SHA-256 `843D45D379F8229E43C1FF0EAC982997059B65CFF348638B65FBCED791D8B5C5`；部署 ID `20260901-230923-817`，`QuantumCheckpoint : 1`、旧 Lua 探针为 `0`。
- 尚未证明：真实普通地牢中的反射结构导入、首次波次拦截时序、向下生命写回后的 UI/流程一致性，以及恢复后完整战斗流程。不能仅凭编译和离线测试称为成功。

## 已验证能力

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

1. 在真实普通 `DUNGEON` 连续执行至少三轮“自动/手动保存 → 返回菜单或重启 → 读取 → 正常完成小关”，核对恢复报告和日志。
2. 分别覆盖目标生命低于、等于和高于新战斗初始生命的样本，并确认 UI 与权威 getter 一致。
3. 用 v0.9.5 先确认第一、第二小关分别自动写入波次 `0/1`，再执行“确认文件存在 → 战斗内读取”的分段复测；若仍失败，以 `route-c-trace.log` 的最后阶段和原因文本为准继续收敛。
4. C 路线稳定后再决定 UI、检查点生命周期及精确恢复的下一条最小增量。

具体构建要求和当前阻塞见 [cpp-development.md](cpp-development.md)。
