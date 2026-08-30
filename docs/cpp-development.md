# C++ 开发环境与只读导出器

## 目标

本阶段先建立两条恢复路线共用的 C++ 基础，不执行恢复：

- 使用 UE4SS 反射读取对象和属性，不硬编码内存偏移。
- 把当前战斗对象导出为带版本号的 JSON 状态报告。
- 用报告确认完整卡组、缓存区、敌人实例和控制器究竟包含哪些字段，再决定恢复路线。

`cpp/QuantumCheckpoint` 已包含只读导出器源码。编译并安装后，`Ctrl+F1` 请求导出，文件写入 UE4SS 的 `Mods/QuantumCheckpoint/Reports`。导出器读取反射属性，并调用零参数、带返回值的只读 getter；不调用卡牌、波次或生命修改函数。旧版曾使用 `Ctrl+F11`，实测会同时触发游戏自身的窗口模式切换，因此已更换。

## 固定版本

当前游戏安装使用 UE4SS `v3.0.1` zDEV，因此 C++ Mod 必须以相同版本和兼容的 C 运行库构建。项目的 `CMakeLists.txt` 默认要求完整源码位于：

```text
vendor/RE-UE4SS-v3.0.1
```

第三方源码、生成头文件和构建产物都位于被忽略的目录，不进入本仓库。

## 已验证的构建环境

2026-08-30 已完成并实际通过全量编译：

- Visual Studio 2022 Build Tools `17.14.39`。
- 固定使用 MSVC v143 `14.38.33130`（编译器 `19.38.33145`）。UE4SS v3.0.1 的旧 UEPseudo 源码不能直接通过本机最新的 MSVC `14.44` 编译，因此两个工具集并排保留。
- Visual Studio 自带 CMake `3.31.6-msvc6`；脚本会自动发现它，不要求 `cmake.exe` 位于终端 `PATH`。
- Windows SDK `10.0.28000.0`。这是 Windows 11 SDK，但 CMake 已验证能以 Windows `10.0.19045` 为目标构建，无需另外安装 Windows 10 SDK。
- Rust `nightly-2024-02-14-x86_64-pc-windows-msvc`，用于编译 UE4SS 的 `patternsleuth_bind`。
- RE-UE4SS v3.0.1：`d935b5b23bac03b65c14ae38382b02007204cc2e`。
- UEPseudo：`d09b7218bfe7392adeffb500fdeee0b42ca1cd27`；patternsleuth：`33e731e99f2a6bb7f65a8e95e89fd1c06ce9d1d2`。

UEPseudo 的授权访问已通过 GitHub SSH 验证。构建脚本只在自身的子进程中把 GitHub HTTPS URL 改写为 SSH，不修改用户的全局 Git 配置。

UE4SS v3.0.1 还把多个 FetchContent 依赖指向可移动分支。顶层工程已把 `ImGuiColorTextEdit`、`IconFontCppHeaders` 和 Tracy 固定到已验证的历史提交，避免上游分支变化或新版 CMake 最低版本要求破坏历史构建。

## 环境检查与构建

只读检查：

```powershell
.\scripts\Check-CppPrerequisites.ps1
```

依赖齐全后构建：

```powershell
.\scripts\Build-CppMod.ps1
```

构建配置默认为 `Game__Shipping__Win64`。生成 DLL 后仍需单独经过安装脚本和启动验证；C++ 模块可以与现有 Lua 研究探针并行加载。

当前已验证产物：

```text
build/cpp-vs17-14.38/Output/Game__Shipping__Win64/bin/QuantumCheckpoint.dll
```

首次构建会下载并编译 UE4SS 的第三方依赖，耗时明显长于增量构建。构建脚本固定 Visual Studio 17 生成器、MSVC 14.38、UE4SS 的六种多配置名称和 Rust nightly，以规避旧版 Corrosion 在首次配置时产生空输出目录的问题。

2026-08-31 已完成五版运行验证。当前已验证的 v0.5/schema 5 本地产物为 x64 DLL，大小 `449536` 字节，导出 `start_mod` 与 `uninstall_mod`，部署 SHA-256 为 `8332C4D2F3E65FCA389E8A53AD63531146493D1A5B7B4F438010B7D552A0FEAD`。构建产物不提交仓库；重新构建后散列可以变化。

v0.3 已在真实战斗中导出 99 个对象且无崩溃，包括完整玩家运行牌组、各牌区实例、20 个场上槽位、敌方实例、效果显示与通用/特殊计数器。详细结果见 [phase-3-cpp-readonly-export.md](phase-3-cpp-readonly-export.md)。

v0.4 记录 getter 原生地址并完成离线调用链分析；v0.5 只读导出私有卡牌状态。在包含 17 张活动卡、敌我受伤、三个敌人与全部主要玩家牌区的复杂样本中，私有生命/回合字段与公开 getter 逐张一致。偏移只适用于已记录 SHA-256 的当前游戏 EXE，详见 [phase-4-native-card-state.md](phase-4-native-card-state.md)。

v0.6.1 加入带 EXE 大小、getter RVA、setter 机器码、内存页权限和数值范围门禁的生命写入探针。实测目标生命 `1→2→1`，临时值直接读回和恢复后公开 getter 均符合预期，无崩溃或残留变化。该版本 DLL 大小为 `470016` 字节，部署 SHA-256 为 `DE43B09134181B90FA82B619C799A1FA69F0B51B81AC3ED0D1317601A058A2F6`。详细安全边界见 [phase-5-guarded-health-write.md](phase-5-guarded-health-write.md)。

可回滚部署流程：

```powershell
.\scripts\Install-CppMod.ps1 -DryRun
.\scripts\Install-CppMod.ps1
```

安装器在游戏进程存在时拒绝写入。它按 UE4SS v3.0.1 的要求将产物复制为 `Mods/QuantumCheckpoint/dlls/main.dll`，更新现有 `mods.txt`，并记录 DLL 和 `mods.txt` 的备份及散列。回滚使用：

```powershell
.\scripts\Uninstall-CppMod.ps1
```

回滚器只覆盖仍与部署散列一致的文件；若 `mods.txt` 后来有其他改动，则保留那些改动并只把 `QuantumCheckpoint` 设为禁用。DLL 删除也通过移动到备份目录完成。

## 生成游戏 CXX 头文件

Lua 探针新增 `Ctrl+F10`，调用 UE4SS 的 `GenerateSDK()`。需要完全重启游戏以加载新脚本，进入战斗并等待动作队列稳定后再按键。生成结果属于本地逆向产物，不提交仓库；它主要用于核对 Quantum 自定义类、结构和属性名称，不能把生成的内存布局直接视为完全准确。

已有对象转储也可以离线提取关键类和结构的反射成员：

```powershell
.\scripts\Extract-QuantumSchema.ps1 -ObjectDumpPath .\logs\<timestamp>\UE4SS_ObjectDump.txt
```

默认报告写入 `runtime/quantum-schema-inventory.md`，同样不进入版本库。
