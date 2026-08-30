# Quantum Protocol Checkpoint Mod

《Quantum Protocol》局内检查点 Mod 的可行性研究与实验原型。

目前已确认：可以通过 UE4SS 读取战斗对象、记录卡牌区域与场上位置，并调用部分原生函数恢复生命或重建玩家牌组。但“退出游戏后仍可读取的完整检查点”尚未实现，本仓库当前不是可直接游玩的成品 Mod。

## 当前结论

我们正在比较两条实现路线，尚未最终选型：

- 小关开头检查点：敌人可按关卡原生逻辑生成，但需要精确恢复玩家手牌、场上、墓地等状态。
- “重编程”完成后检查点：玩家牌区可按原生重编程规则重新洗牌并抽五张，避免保存手牌、场上与墓地；代价是必须精确恢复保存时的敌人状态。

无论采用哪条路线，可靠的跨进程恢复都需要自定义持久化文件、重新进入对应战斗、恢复关卡上下文，并处理游戏结束界面与输入状态。详见：

- [需求基线](docs/requirements.md)
- [架构路线对比](docs/architecture-options.md)
- [技术状态与实验结论](docs/technical-status.md)
- [CXX SDK 分析](docs/sdk-analysis.md)
- [C++ 只读导出实验](docs/phase-3-cpp-readonly-export.md)
- [文档索引](docs/README.md)

## 目录

- `src/QuantumCheckpointProbe`：UE4SS Lua 研究原型。
- `cpp/QuantumCheckpoint`：只读战斗状态导出器的 C++ 源码；已在本机构建通过。
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

## C++ 只读导出器

构建并进行无写入部署检查：

```powershell
.\scripts\Build-CppMod.ps1
.\scripts\Install-CppMod.ps1 -DryRun
```

完全退出游戏后安装：

```powershell
.\scripts\Install-CppMod.ps1
```

安装器会把 DLL 部署为 `Mods\QuantumCheckpoint\dlls\main.dll`，在现有 `mods.txt` 中加入 `QuantumCheckpoint : 1`，并把精确回滚材料保存在被 Git 忽略的 `backups/cpp` 与 `runtime` 目录。它不会禁用 Lua 研究探针。进入战斗后按 `Ctrl+F1`，只读报告应生成到 `Mods\QuantumCheckpoint\Reports`。

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
