# 技术状态与已知事实

最后更新：2026-08-30。

## 环境

- 游戏：Quantum Protocol，Windows / Unreal Engine 4。
- 已测试安装目录：`F:\SteamLibrary\steamapps\common\Quantum Protocol`。
- 注入/反射工具：UE4SS 3.0.1 zDEV。
- 当前实现：UE4SS Lua 研究探针加 C++ 只读反射导出器；尚不具备跨进程恢复。
- 正式跨进程恢复倾向 UE4SS C++ Mod。第一版只读反射导出器已使用 RE-UE4SS v3.0.1、UEPseudo、MSVC 14.38 和固定 Rust nightly 成功构建，并已通过可回滚脚本部署到游戏目录；尚待首次进程内加载与 `Ctrl+F11` 导出验证。
- Windows 11 SDK `10.0.28000.0` 已被 CMake 正确选中并以 Windows `10.0.19045` 为目标完成构建，不需要额外安装 Windows 10 SDK。
- 已在战斗场景成功生成 CXX SDK：662 个头文件，关键 Quantum 类、结构和函数签名均已取得。详细证据见 [sdk-analysis.md](sdk-analysis.md)。

## 已验证能力

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

敌人侧仍没有同等完整的单一结构。`InGameCard` 的卡牌标识、当前生命、位置和回合计数主要表现为 getter，持续效果还分散在卡牌和覆盖层相关对象中。因此敌人精确恢复依然是“重编程”路线的主要未知量。

CXX SDK 进一步确认：已公开的卡牌状态修改函数主要是 `Action_*_Visuals` 表现层回调，不能据此写回权威战斗状态。特殊关卡还存在带独立状态的 Spawner 子类、教程过滤器和 Boss 监视器；精确恢复范围不能只限于敌方卡牌和波次索引。

## 存档安全边界

- 研究原型不读取、不覆盖游戏原生 `.sav`。
- 日志、崩溃转储、UE4SS 二进制、用户存档备份和部署运行清单不得进入版本库。
- 成品检查点应使用独立目录、格式版本、校验、大小上限和原子替换。
- 所有恢复测试应先使用可丢弃的游戏流程；没有状态签名验证前，不宣称“精确恢复”。

## 下一步需要获得的证据

1. 在游戏中验证已部署 C++ DLL 的加载与 `Ctrl+F11` 导出，不执行恢复。
2. 用 C++ 只读导出 `GameDeckRun` 的完整牌组、缓存区和全部升级字段，不经过 Lua 大结构返回值。
3. 只读导出敌人全部实例状态、持续效果、生成控制器子类和关卡辅助 Actor。
4. 验证从菜单进入指定流程/关卡的安全路径。
5. 找到卡牌私有权威状态的安全读取与写回路径，再回到 [architecture-options.md](architecture-options.md) 做最终路线决策。

具体构建要求和当前阻塞见 [cpp-development.md](cpp-development.md)。
