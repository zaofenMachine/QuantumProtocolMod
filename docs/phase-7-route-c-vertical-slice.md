# 第七阶段：路线 C 普通小关语义重开

日期：2026-08-31
版本：v0.9.5-dev
状态：v0.9.0 首轮实机读取触发 abort；v0.9.1 无存档读取已安全拒绝；v0.9.2/v0.9.3 依次暴露原生 Spawner、空缓存区与钩子层级误判；v0.9.4 手动保存成功但自动钩子未命中；v0.9.5 已部署低频波次观测，等待自动保存复测。

## 本阶段要闭合什么

路线 C 先回答一个比精确快照更小、但端到端完整的问题：能否把一个普通 `DUNGEON` 小关保存到独立文件，在返回菜单或重启后重新进入同一战斗，以保存的波次原生生成敌人，用保存的牌组和缓存重建玩家牌区，并恢复生命。

它不保存保存瞬间的手牌、牌序、玩家场上/墓地、敌人伤势、效果、临时属性或 RNG。恢复后重洗牌并重新抽牌属于预期结果。

## 已实现链路

保存阶段在低频观测到活动 Spawner 的 `currentWaveIndex` 稳定或变化后延迟 1.5 秒自动触发，也可用 `Ctrl+Shift+F5` 手动触发。只有完整战斗对象、`OPEN` 状态、无活动卡牌提示、普通 `DUNGEON`、普通尺寸 Spawner 且无已知教程/Boss 伴生对象时才写盘。

`route-c.json` 保存 schema/kind、UTC 时间、游戏 EXE 完整指纹、模式、来源关卡、角色与关卡结构、活动牌组、缓存、DeckRun 摘要、玩家生命/上限、波次及 Spawner 类/尺寸。文件上限为 2 MiB；规范字段负载计算校验和；写入采用同目录临时文件加 Windows 原子替换，并保留上一份 `.bak`。

`Ctrl+Shift+F6` 读取时先完成格式、校验和、模式、Spawner 和 EXE 指纹检查。随后：

1. 向活动 GameInstance 导入保存的角色、关卡、来源关卡、牌组和缓存；若重载前失败则尽力回滚原值。
2. 调用 `reloadBattleArea()` 创建新战斗上下文。
3. 具体原生 UFunction 前置钩子只对匹配 Spawner 类的第一次 `spawnWaveIndex` 改写波次参数，并记录该新实例。
4. 等待新战斗进入 `OPEN`，确认活动 Spawner 就是被拦截实例，暂停动作队列。
5. 调用 `resetPlayerBoard()`，等待后调用 `LoadPlayerCardsStart()`，再恢复生命并恢复动作队列。
6. 验证波次、生命、活动牌组和缓存；结果写入 `Reports/route-c-restore-*.json`。总事务超过 45 秒则失败。

## 安全边界

- 只接受 EXE 大小 `82718720`、SHA-256 `0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6`。
- 只接受 `DUNGEON`，拒绝 `DUNGEON_EVENT` 和 `INFINITE`。
- Spawner 反射类尺寸只接受 `0x270..0x280`；教程过滤器、Boss 入场/Waystone 监视器存在时拒绝保存。
- 不读取或覆盖游戏原生 `.sav`。
- 失败报告不是成功；没有真实恢复报告前不宣称路线 C 已闭环。
- 旧的 `Ctrl+Shift+F12/F9` 私有生命/回合探针不属于正常恢复流程，只能用于可丢弃实验局。

## 离线验证

`Build-CppMod.ps1` 会构建并运行 `QuantumCheckpointPersistenceTests`。当前覆盖：

- 含中文、引号和复杂 UE 文本的 JSON 往返。
- 修改已序列化生命而不更新校验和时拒绝。
- `INFINITE` 模式拒绝。
- 有额外状态尺寸的 Spawner 拒绝。

当前结果为 `1/1` 通过。C++ DLL 也已成功链接；UE4SS 上游头文件仍产生既有 C4251 警告，没有新增编译错误。当前 v0.9.5 产物大小为 `643072` 字节，SHA-256 为 `843D45D379F8229E43C1FF0EAC982997059B65CFF348638B65FBCED791D8B5C5`，导出 `start_mod` 与 `uninstall_mod`。同散列 DLL 已部署到游戏目录，部署清单 ID 为 `20260901-230923-817`；前序 DLL 已移动到部署备份目录，可由回滚材料恢复。

## 首轮实机失败与 v0.9.1 修正

v0.9.0 分别在战斗内 Esc 菜单、返回标题以及重启后主界面读取，三次都由 `ucrtbase!abort` 终止；WER 签名一致，为 `0xc0000409 / FAST_FAIL 7`。预期检查点和恢复报告均未生成，故当前证据不支持把问题归因于“读取前所在界面”。

检查后确认复杂 Getter 的返回参数缓冲区没有先调用 FProperty 初始化。v0.9.1 让 Getter 与带参数反射调用共用初始化/析构缓冲区，并把不存在检查点的读取改成无异常的文件状态检查；`on_update` 增加最终异常边界。另新增 `Mods/QuantumCheckpoint/route-c-trace.log`，每条阶段记录都强制刷盘，以便即使再次退出也能知道停在热键、读文件、牌组 Getter、写盘、导入还是重载。修正尚待实机确认。

v0.9.1 的无存档读取随后在两个独立进程中安全拒绝，没有新增崩溃。首次保存诊断确认当前战斗为 `neskaraDungeon1 / Type=DUNGEON / OPEN`，但生成器是没有 `_C` 后缀的原生 `/Script/Quantum.NeskaraDungeon1Spawner`，旧名称筛选因此误报“不完整战斗”。v0.9.2 改从 CardEngine 的 `mEnemySpawnController` 权威引用取得并验证 `SpawnController` 实例，同时从 `mHUDBottomBar` 锚定同一战斗 HUD。

v0.9.2 的下一次实机手动保存已通过四个对象门禁并读取活动牌组，但合法空 `getActiveStorage()` 被误判为 Getter 不可用；没有生成检查点。该进程也没有任何 Mod 自动捕获记录，原因是正常推进命中早期实机已确认的 `spawnNextWave()`，而旧代码只监听 `spawnWaveIndex(int)`。v0.9.3 将空数组规范化为 `()`，并用 `spawnNextWave` 后的权威 `currentWaveIndex` 调度延迟捕获；拒绝原因同时持久化到轨迹。

v0.9.3 新进程确认两个波次 UFunction 和 SpawnController 反射类均初始化成功，但进入第一小关仍无自动调度轨迹。根因是这些原生调用未经过全局 ProcessEvent 回调。v0.9.4 改用 `UObjectGlobals::RegisterHook(UFunction*)` 注册具体原生 UFunction 的前/后置回调，与早期实机成功的 Lua `RegisterHook` 机制一致。

v0.9.4 随后在第一小关用热键首次完整写出检查点，证明复杂 Getter、空缓存、对象锚定和原子写盘链路已经贯通；文件记录 `cometDungeon1`、`CometDungeon1Spawner`、波次 `0`、生命 `9/9`。但正常进入第二小关后仍没有任何钩子调度记录，检查点也未覆盖。v0.9.5 因而改为每 750 ms 低频观测 CardEngine 权威 Spawner，只在 `OPEN` 状态下检测到实例/World/波次变化时安排一次捕获；具体原生钩子仅保留给恢复初始生成重定向。

## 真实验收步骤

所有测试使用可丢弃流程，并完全退出游戏后替换 DLL。v0.9.5 继续按以下分段顺序，不再一次做完整三轮：

1. 完全重启游戏，在标题/主界面且确认没有 `route-c.json` 时按一次 `Ctrl+Shift+F6`；应只安全拒绝，不得退出。
2. 进入无教程、非 Boss、非事件、非无限的普通地牢小关，等待所有动作结束。
3. 记录关卡、角色、生命和当前波次；确认自动保存日志，或按 `Ctrl+Shift+F5`，随后先确认 `Checkpoint/route-c.json` 已存在。
4. 复制或查看 `route-c-trace.log`；末尾应到 `capture.complete`，再执行首次读取。
5. 第一次可停留在战斗内读取；通过后再覆盖返回标题和完全重启两种入口。
6. 等待重载完成，确认进入同一小关/波次，敌人为该波次的原生初始状态，玩家牌组与缓存构成正确、生命正确且可以正常出牌。
7. 正常打完本小关，确认不会卡住胜负、输入或动作队列。
8. 检查最新恢复报告为 `passed`；若为 `failed` 或退出，同时收集 UE4SS 日志、检查点、报告和 `route-c-trace.log`。
9. 最终至少连续完成三轮，并覆盖目标生命低于、等于和高于新战斗初始生命的情况。

只有上述条件全部满足，路线 C 才算运行闭环；此前不开始精确恢复探索。
