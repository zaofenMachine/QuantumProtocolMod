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

## 当前环境缺口

2026-08-29 的检查结果：

- 本机未发现 CMake、MSVC、MSBuild、Ninja 或 clang-cl。
- zDEV 发布包只有运行时 DLL/PDB，没有构建 C++ Mod 所需的头文件和导入目标。
- 官方 `v3.0.1` 源码可以下载，但 `deps/first/Unreal`（UEPseudo）是受限子模块。
- 当前 GitHub 凭据访问 UEPseudo 返回 `Repository not found`。

UE4SS 官方流程要求 GitHub 账号与 Epic Games 账号关联，并接受 EpicGames GitHub 组织邀请，随后才能初始化全部子模块。缺少 UEPseudo 时，CMake 会主动中止并给出明确错误，避免产生与当前 UE4SS 二进制不兼容的 DLL。

## 环境检查与构建

只读检查：

```powershell
.\scripts\Check-CppPrerequisites.ps1
```

依赖齐全后构建：

```powershell
.\scripts\Build-CppMod.ps1
```

构建配置默认为 `Game__Shipping__Win64`。生成 DLL 后仍需单独经过安装脚本和启动验证；在成功构建前，当前游戏只运行 Lua 研究探针。

## 生成游戏 CXX 头文件

Lua 探针新增 `Ctrl+F10`，调用 UE4SS 的 `GenerateSDK()`。需要完全重启游戏以加载新脚本，进入战斗并等待动作队列稳定后再按键。生成结果属于本地逆向产物，不提交仓库；它主要用于核对 Quantum 自定义类、结构和属性名称，不能把生成的内存布局直接视为完全准确。

已有对象转储也可以离线提取关键类和结构的反射成员：

```powershell
.\scripts\Extract-QuantumSchema.ps1 -ObjectDumpPath .\logs\<timestamp>\UE4SS_ObjectDump.txt
```

默认报告写入 `runtime/quantum-schema-inventory.md`，同样不进入版本库。
