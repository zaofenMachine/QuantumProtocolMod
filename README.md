# Quantum Protocol Checkpoint Mod

《Quantum Protocol》局内检查点 Mod 的可行性研究与实验原型。

当前 v0.33.0 沿用路线 C“普通地牢小关语义重开”，叠加受限的玩家精确恢复：原生牌序、手牌数量、场格、生命与攻击修正、计数器、抽牌进度，以及春生成的一张额外苹果。恢复在游戏线程执行，精确层失败后重新加载一次纯 Route C。

本轮补齐原生手牌生命增益，支持两张同名苹果分别保留 3/3 与 2/2。zones/trash/field 新格式统一检查场外状态，并强制关联手牌生命补充，防止缺文件时静默丢失增益。最终构建已通过 11 组恢复及依赖故障、实际出牌和安全回退验证；任意手牌动态、更多生成卡、角色能力卡和效果动作历史仍有缺口。

## 当前结论

当前不再同时推进两条高成本精确路线。未启用精确补充层时，路线 C v1 的基础语义是：

- 在普通 `DUNGEON` 小关生成稳定后自动覆盖唯一检查点，也可按 `Ctrl+Shift+F5` 手动保存。
- 读取时重新进入保存的战斗，把首次生成重定向到保存的波次，再用保存的活动牌组和缓存区重建玩家牌区，重新洗牌并抽牌，最后恢复生命。
- 不保证原手牌、牌库顺序、场上、墓地、敌人受伤或效果与保存画面一致；这是“小关语义重开”，不是精确快照。
- 首版显式排除无限模式、地牢事件、教程、Boss 伴生逻辑和带额外状态的 Spawner。

2026-09-20 用户授权恢复推进“小关重开 + 玩家精确恢复”，继续搁置“重编程重开”和敌人精确恢复。此前已验证真实牌序、空牌区、六／七张手牌、满手牌暂存，以及场地攻击、生命、计数器和一张生成苹果的恢复。生成卡仍保留战斗 9 张／永久牌组 8 张；LEVEL 修正、非零独立生命调整和效果动作历史仍不恢复。

本轮用奥克塔娃原生技能复现旧版手牌增益丢失，并新增 HAND HEALTH schema 1 与布局依赖。首版正式 DLL 已通过默认手牌、同名苹果不同生命的新进程恢复与再保存、手牌增益和普通场地混合，以及恢复后正常出牌对照。最终正式 DLL 的 4/4 CTest、动态手牌新进程与再保存恢复、缺失/损坏/错配三种依赖降级均通过，报告编码问题已修正；旧格式、生成卡和空手牌回归也已通过；真实弹幕手牌计数会明确拒绝精确捕获，作为下一切片样本。继续以保存前后状态比较和原生操作作为验收依据。详见：

- [手牌生命修正与场外保存边界](docs/phase-32-player-hand-health.md)
- [当前工作进度](docs/WIP-current-progress-2026-09-10.md)
- [玩家场地生命修正恢复](docs/phase-26-player-field-health.md)
- [玩家计数器原生观测](docs/phase-27-native-player-counters.md)
- [玩家场地计数器恢复](docs/phase-28-player-field-counters.md)
- [玩家等级来源与 LEVEL 条目](docs/phase-29-native-player-level.md)
- [春生成的额外苹果](docs/phase-30-generated-player-card.md)
- [生成卡退场后的空场恢复](docs/phase-31-generated-empty-field.md)
- [需求基线](docs/requirements.md)
- [架构路线对比](docs/architecture-options.md)
- [技术状态与实验结论](docs/technical-status.md)
- [CXX SDK 分析](docs/sdk-analysis.md)
- [C++ 只读导出实验](docs/phase-3-cpp-readonly-export.md)
- [原生卡牌状态验证](docs/phase-4-native-card-state.md)
- [受控生命值写入验证](docs/phase-5-guarded-health-write.md)
- [受控回合计数写入验证](docs/phase-6-guarded-turn-write.md)
- [路线 C 垂直切片](docs/phase-7-route-c-vertical-slice.md)
- [精确状态差异与未来小关计划](docs/phase-8-exact-state-gap.md)
- [固定顺序恢复初始牌库与手牌](docs/phase-9-fixed-player-zones.md)
- [复杂战斗差异与向下生命恢复](docs/phase-10-complex-combat-gap.md)
- [角色充能精确恢复](docs/phase-11-character-charge.md)
- [玩家墓地精确恢复](docs/phase-12-player-trash.md)
- [回合进度实验及后续修正](docs/phase-13-turn-progress.md)
- [受限场地、真实抽牌与失败回退](docs/phase-15-player-field-and-draw-authority.md)
- [玩家场地与墓地共存](docs/phase-16-player-field-and-trash.md)
- [玩家牌区真实顺序](docs/phase-17-native-player-zone-order.md)
- [跨玩家牌区的同名副本](docs/phase-18-player-duplicate-zones.md)
- [空牌库与空手牌](docs/phase-19-empty-player-zones.md)
- [手牌数量、满手牌暂存与当前边界](docs/phase-20-player-hand-capacity.md)
- [恢复对象扫描优化](docs/phase-21-restore-object-filtering.md)
- [原生攻击力与属性修正观测](docs/phase-22-native-player-statistics.md)
- [玩家场地攻击修正恢复](docs/phase-23-player-field-attack.md)
- [攻击修正顺序与生命周期](docs/phase-24-player-attack-lifecycle.md)
- [玩家生命上限观测](docs/phase-25-native-player-health.md)
- [文档索引](docs/README.md)

## 目录

- `src/QuantumCheckpointProbe`：UE4SS Lua 研究原型。
- `cpp/QuantumCheckpoint`：战斗状态导出器与受控原生写入探针的 C++ 源码；已在本机构建并实测。
- `deployment`：开发探针使用的 UE4SS 配置。
- `scripts`：安装、卸载和收集日志的 PowerShell 脚本。
- `docs`：需求、架构决策和阶段实验记录。
- `vendor`、`runtime`、`logs`、`backups`：本地依赖和运行产物，不进入版本库。

## 安装研究探针

当前脚本默认游戏安装在：

```text
F:\SteamLibrary\steamapps\common\Quantum Protocol
```

并预期 UE4SS 3.0.1 开发包位于 `vendor/ue4ss-3.0.1-dev`。在 PowerShell 中执行：

```powershell
.\scripts\Install-DevProbe.ps1
```

安装器只向 `Quantum\Binaries\Win64` 部署文件；替换已有文件前会备份，并生成本地部署清单。

## 研究探针按键

- `Ctrl+F2`：在 `Ctrl+F3` 完成后，测试 `LoadPlayerCardsStart()` 重建玩家牌区。
- `Ctrl+F3`：测试原生 `resetPlayerBoard()`；会破坏当前战斗中的玩家牌区，仅用于可丢弃的测试局。
- `Ctrl+F5`：保存一份仅存在于内存中的研究快照。
- `Ctrl+F6`：把生命恢复到快照记录值，且不超过当前上限。
- `Ctrl+F7`：只读比较当前卡牌区域与快照。
- `Ctrl+F8`：只读枚举当前加载的 Quantum 对象。
- `Ctrl+F9`：请求 UE4SS 完整对象转储。
- `Ctrl+F10`：调用 UE4SS CXX Header Generator，把当前已加载类型导出到本地 `CXXHeaderDump`；建议进入战斗且状态稳定后使用。

Lua 版 `Ctrl+F1` 敌人清空/重建实验已移除：实测会先触发原生小关完成逻辑，从而直接进入下一小关。当前 `Ctrl+F1` 由 C++ 只读导出器使用。UE4SS 热重载也已在随附配置中关闭；修改 Lua 或 C++ DLL 后请完全退出并重启游戏。

C++ 构建前提、已验证工具链和反射结构提取方法见 [C++ 开发说明](docs/cpp-development.md)。

## C++ 路线 C 原型

构建并进行无写入部署检查：

```powershell
.\scripts\Build-CppMod.ps1
.\scripts\Install-CppMod.ps1 -DryRun
```

完全退出游戏后安装：

```powershell
.\scripts\Install-CppMod.ps1
```

安装器会把 DLL 部署为 `Mods\QuantumCheckpoint\dlls\main.dll`，在现有 `mods.txt` 中加入 `QuantumCheckpoint : 1`，并把精确回滚材料保存在被 Git 忽略的 `backups/cpp` 与 `runtime` 目录。若旧 Lua 研究探针存在，安装器会在本次 C++ 部署中将其禁用；回滚时会恢复部署前配置。

v0.33.0 路线 C 与精确补充切片的热键和输出：

- `Ctrl+Shift+F5`：在受支持的稳定普通小关手动保存；每次正常波次生成后也会自动保存。
- `Ctrl+Shift+F6`：读取唯一检查点并执行语义重开。
- `Ctrl+F1`：保留只读对象报告。
- 检查点：`Mods\QuantumCheckpoint\Checkpoint\route-c.json`，原子替换并保留 `.bak`。
- 精确补充：`Mods\QuantumCheckpoint\Checkpoint\route-c-exact-spawn-plan.json`；与主检查点校验和绑定，缺失或失败时安全降级为路线 C。
- 牌库/手牌补充：`Mods\QuantumCheckpoint\Checkpoint\route-c-exact-player-zones.json`；schema 3 要求所有活动玩家卡位于牌库/手牌且总集合完全匹配，按原生顺序恢复手牌数量，并核验游戏公开容量。
- 墓地补充：`Mods\QuantumCheckpoint\Checkpoint\route-c-exact-player-trash.json`；schema 4 要求受支持卡牌完整分布于牌库、手牌和墓地，场上/待处理区为空。支持已验证的一张额外苹果，通过原生 `DEFAULT` MoveCard 重建并严格复核三区顺序。
- 手牌生命补充：`Mods\QuantumCheckpoint\Checkpoint\route-c-exact-player-hand-health.json`；schema 1 按原生 HAND 位置记录完整 HEALTH 修正。当前仅支持非负修正且当前生命等于上限；新布局中非空手牌必须关联此文件。
- 角色充能补充：`Mods\QuantumCheckpoint\Checkpoint\route-c-exact-character-charge.json`；只保存低于满充阈值的值，恢复时通过公开增量 API 写回并由 Getter 复核。
- 玩家场地补充：`Mods\QuantumCheckpoint\Checkpoint\route-c-exact-player-field.json`；schema 7 限受支持卡牌、空 PENDING，统一恢复墓地、场格、生命、攻击和计数器。按真实牌序分配同名副本，允许空牌库/手牌和已验证的一张额外苹果；完整集合和原生暂存规划仍须通过验证。
- 回合进度补充：`Mods\QuantumCheckpoint\Checkpoint\route-c-exact-turn-progress.json`；schema 2 成组保存全局回合、抽牌基础倒计时与修正量、累计威胁；同时核验游戏自行计算的抽牌缓存。旧 schema 1 只用于读取诊断，需重新保存以启用战斗补充。
- 恢复结果：`Mods\QuantumCheckpoint\Reports\route-c-restore-*.json`。
- 诊断轨迹：`Mods\QuantumCheckpoint\route-c-trace.log`；F5/F6 和各高风险阶段都会立即刷盘。

当前 schema 2 主检查点仅接受已验证游戏 EXE 的完整 SHA-256，且只允许普通 `DUNGEON`、无活动提示、稳定 `OPEN` 状态和无额外状态的普通 Spawner。补充文件分别维护格式版本，并在写入前检查依赖。缺少有效回合补充时禁用场地/墓地等战斗补充；缺少精确玩家牌区时也不单独写回合。新布局所需 HAND 文件缺失、损坏或错配时，预检禁用玩家布局、回合和角色充能层；旧格式保持兼容，但不声明新的手牌动态覆盖。写后失败会重新加载纯 Route C，报告明确区分 requested、preflight-route-c 与 semantic-fallback。项目仍不宣称完整中途战场快照。

旧的生命与回合写入探针仍保留用于可丢弃测试局，但不属于路线 C 的日常操作。路线 C 的实现和验收步骤见 [第七阶段报告](docs/phase-7-route-c-vertical-slice.md)。

回滚 C++ 模块：

```powershell
.\scripts\Uninstall-CppMod.ps1
```

不要在 UE4SS 3.0.1 Lua 中调用 `GI_Quantum_C:getActiveDecklist()`。它在传递大型 `Decklist` 返回结构时会导致原生访问冲突，原 `Ctrl+F4` 诊断入口已移除。

## 日志与卸载

收集日志：

```powershell
.\scripts\Collect-Logs.ps1
```

卸载探针：

```powershell
.\scripts\Uninstall-DevProbe.ps1
```

卸载器只处理部署清单记录的文件；安装后被修改过的文件会保留并报告。
