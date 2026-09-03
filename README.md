# Quantum Protocol Checkpoint Mod

《Quantum Protocol》局内检查点 Mod 的可行性研究与实验原型。

当前路线 C“普通地牢小关语义重开”的核心垂直切片已经闭环。v0.9.13 使用 schema 2 独立检查点、原子写入、完整 EXE 门禁、新战斗重载、BeginPlay 分阶段语义重放、原生波次生成，以及牌组/缓存/新掉落物/生命恢复。v0.10.1 又以独立、可降级的精确补充文件恢复 `SpawnController.spawnList`，首份实机对照中未来 29 个小关计划逐项一致。项目仍不保证手牌/牌库和中途战场的完整精确复原；更多普通关卡、非满生命和入口矩阵也仍属于发布前回归范围。

## 当前结论

当前不再同时推进两条高成本精确路线。路线 C v1 的语义是：

- 在普通 `DUNGEON` 小关生成稳定后自动覆盖唯一检查点，也可按 `Ctrl+Shift+F5` 手动保存。
- 读取时重新进入保存的战斗，把首次生成重定向到保存的波次，再用保存的活动牌组和缓存区重建玩家牌区，重新洗牌并抽牌，最后恢复生命。
- 不保证原手牌、牌库顺序、场上、墓地、敌人受伤或效果与保存画面一致；这是“小关语义重开”，不是精确快照。
- 首版显式排除无限模式、地牢事件、教程、Boss 伴生逻辑和带额外状态的 Spawner。

精确恢复路线 A/B 不再冻结，但先做恢复前后只读状态签名与差异报告，再依据证据选择最小写回切片。详见：

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

v0.10.1 路线 C 与首个精确补充切片的热键和输出：

- `Ctrl+Shift+F5`：在受支持的稳定普通小关手动保存；每次正常波次生成后也会自动保存。
- `Ctrl+Shift+F6`：读取唯一检查点并执行语义重开。
- `Ctrl+F1`：保留只读对象报告。
- 检查点：`Mods\QuantumCheckpoint\Checkpoint\route-c.json`，原子替换并保留 `.bak`。
- 精确补充：`Mods\QuantumCheckpoint\Checkpoint\route-c-exact-spawn-plan.json`；与主检查点校验和绑定，缺失或失败时安全降级为路线 C。
- 恢复结果：`Mods\QuantumCheckpoint\Reports\route-c-restore-*.json`。
- 诊断轨迹：`Mods\QuantumCheckpoint\route-c-trace.log`；F5/F6 和各高风险阶段都会立即刷盘。

当前 schema 2 主检查点仅接受已验证游戏 EXE 的完整 SHA-256，且只允许普通 `DUNGEON`、无活动提示、稳定 `OPEN` 状态和无额外状态的普通 Spawner。schema 1 精确补充已经验证可恢复完整未来小关计划；读取仍会重洗牌并重抽，尚未恢复保存瞬间的手牌/牌库分配及中途卡牌战场。

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
