# C++ 开发环境与只读导出器

## 目标

本阶段先建立两条恢复路线共用的 C++ 基础，不执行恢复：

- 使用 UE4SS 反射读取对象和属性，不硬编码内存偏移。
- 把当前战斗对象导出为带版本号的 JSON 状态报告。
- 用报告确认完整卡组、缓存区、敌人实例和控制器究竟包含哪些字段，再决定恢复路线。

`cpp/QuantumCheckpoint` 已包含第一版只读导出器源码。编译并安装后，`Ctrl+F11` 请求导出，文件写入 UE4SS 的 `Mods/QuantumCheckpoint/Reports`。导出器只读取属性，不调用卡牌、波次或生命修改函数。

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

UE4SS v3.0.1 还把两个 FetchContent 依赖指向可移动分支。顶层工程已把 `ImGuiColorTextEdit` 和 `IconFontCppHeaders` 固定到 v3.0.1 发布时期的提交，避免上游分支变化破坏历史构建。

## 环境检查与构建

只读检查：

```powershell
.\scripts\Check-CppPrerequisites.ps1
```

依赖齐全后构建：

```powershell
.\scripts\Build-CppMod.ps1
```

构建配置默认为 `Game__Shipping__Win64`。生成 DLL 后仍需单独经过安装脚本和启动验证；当前游戏仍只运行 Lua 研究探针，尚未加载这份 C++ DLL。

当前已验证产物：

```text
build/cpp-vs17-14.38/Output/Game__Shipping__Win64/bin/QuantumCheckpoint.dll
```

首次构建会下载并编译 UE4SS 的第三方依赖，耗时明显长于增量构建。构建脚本固定 Visual Studio 17 生成器、MSVC 14.38、UE4SS 的六种多配置名称和 Rust nightly，以规避旧版 Corrosion 在首次配置时产生空输出目录的问题。

2026-08-30 的本地产物静态核验结果：x64 DLL，大小 `429056` 字节，导出 `start_mod` 与 `uninstall_mod`，SHA-256 为 `435C67C5C9C3170B4BD3C0CE3FBD65AD4B5A9F66EB0AA30DD748687955318F54`。构建产物不提交仓库，也尚未部署到游戏；下一步需先增加可回滚的安装流程，再由玩家手动启动游戏测试 `Ctrl+F11`。

## 生成游戏 CXX 头文件

Lua 探针新增 `Ctrl+F10`，调用 UE4SS 的 `GenerateSDK()`。需要完全重启游戏以加载新脚本，进入战斗并等待动作队列稳定后再按键。生成结果属于本地逆向产物，不提交仓库；它主要用于核对 Quantum 自定义类、结构和属性名称，不能把生成的内存布局直接视为完全准确。

已有对象转储也可以离线提取关键类和结构的反射成员：

```powershell
.\scripts\Extract-QuantumSchema.ps1 -ObjectDumpPath .\logs\<timestamp>\UE4SS_ObjectDump.txt
```

默认报告写入 `runtime/quantum-schema-inventory.md`，同样不进入版本库。
