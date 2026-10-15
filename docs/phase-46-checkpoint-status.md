# 第四十六阶段：只读查看保存状态

2026-10-15。新增独立命令行工具，分别说明“上次保存尝试的回执记录”和“当前磁盘文件是否与它对应”。沿用 v0.45.0 DLL；本阶段没有启动游戏、重建 DLL 或修改存档。恢复范围仍为小关重开与受限玩家恢复。

## 使用

在仓库根目录运行，需要 PowerShell 7.2 或更高版本；本轮实际验证版本为 7.6.5，未声称实测 7.2。

```powershell
pwsh -NoProfile -File ./scripts/Get-CheckpointStatus.ps1
pwsh -NoProfile -File ./scripts/Get-CheckpointStatus.ps1 -GameRoot 'D:/SteamLibrary/steamapps/common/Quantum Protocol'
pwsh -NoProfile -File ./scripts/Get-CheckpointStatus.ps1 -ModRoot 'D:/CheckpointCopy/QuantumCheckpoint' -Json
```

默认 GameRoot 与安装脚本一致。`-GameRoot` 和 `-ModRoot` 二选一；ModRoot 应直接包含 Checkpoint 和 Reports 子目录。脚本只依赖相邻的 `CheckpointStatus.psm1`，不需要 Python、测试证据目录或运行中的游戏。`-Json` 向标准输出返回结果，不写报告。

| 当前文件关系 | 含义 |
| --- | --- |
| `matching-files` | 主档与已记录部件的 SHA、已知文件头绑定对应；也可能只是 main-only 保存 |
| `no-publication` | 本次回执未发布主档，当前档案来源未由它证明 |
| `stale` | 回执中的主档已不同或缺失 |
| `incomplete` | 主档对应，部分依赖、原路径或绑定不一致／不明 |
| `unknown` | 无回执、无法识别、诊断不完整或读取过程中有变化，不能确认对应关系 |

退出码 `0` 表示完成普通状态观察，包括拒绝、陈旧及依赖不完整，**不表示可恢复**。`2` 表示结果不明，`3` 表示无法读取；入口参数或环境错误返回 `1`，PowerShell 自身启动／参数绑定错误也可能直接终止。

当前安装目录的实际结果是：回执记录 `72816:manual:4` 的主档前拒绝，原因为 `The battle is not in the stable OPEN state`；当前原用户存档未由此回执关联。上阶段清理已还原旧存档，因此这个结果符合实际，不能据此说旧档已经损坏或获得新资格证明。

## 判断边界

工具只读取固定文件名，不跟随回执给出的任意路径，不尝试 `.bak` 回退或修复。拒绝非普通文件、链接与重解析路径；限制文件大小、总读取量和 JSON 深度，严格检查 UTF-8、重复／大小写冲突键、数字与布尔类型、组件依赖和结果自洽。未知版本保留不明，不将旧布局文件的存在视为已保存的玩家资格。

连续两次采样比较文件内容、回执和 checkpoint 文件集合。发现变化就取消对应结论；`two-pass-sampled` 仅表示两次观察一致，不是原子快照或连续监测保证。路径检查也不是针对恶意并发文件系统替换的安全隔离边界。

`matching-files` 不重算完整游戏 FNV 语义，不读取原生对象、不验证技能私有历史，也不执行恢复准入检查。所有结果保持 `restoreAdmissionProven=false` 和 `fullPlayerExactRestorationProven=false`。原始拒绝原因在 JSON 中保留；中文终端显示转义控制字符。

## 验证和归档

最终代码的80项独立测试全部通过，包含严格类型／依赖、同大小同时间但字节不同、缺失／损坏文件、可选组件变化、双次采样期间变化、读取失败、大小限制，以及隔离临时目录中的真实 Junction 和 HardLink 拒绝。默认测试使用合成文件头，不能当作游戏存档或原生恢复证据。

另完成12次命令行检查：9个离线案例和当前安装目录的 JSON、中文及默认参数入口。离线案例来自已冻结的阶段45材料，显式记录复制、路径重映射、删去单个副本或截断回执等转换；原始证据没有改写。

| 离线案例 | 实际关系 |
| --- | --- |
| 成套提交、仅主档提交，回执路径映射到副本 | 两例均 `matching-files`，历史 outcome 分别保留 |
| 真实拒绝回执副本 | `no-publication` |
| 原用户 legacy 文件，无回执 | `unknown` |
| 成功旧回执搭配原用户文件 | `stale` |
| 缺少 HAND、缺少可选 turn | 两例均 `incomplete` |
| 截断回执 | `unknown` |
| 移动副本但保留原记录路径 | `incomplete` |

证据根 `QuantumProtoclMod.runtime-evidence/20261015-checkpoint-status`。冻结7个工具／设计文件及138个本轮证据文件；冻结副本再次执行80项测试和12次 CLI，并与原输出作保留类型的语义比较，仅排除观察时间。23个现场文件（17 checkpoint、4 SaveGames、DLL、latest 回执）的内容和原数据目录文件集合均未改变。无新增实机恢复测试。

冻结 manifest `943D8EC707B29811AAEC60C8925FEF9242485EFA05A0443757D0ACC51D5544A0`。复验入口 `phase46-frozen-revalidation-20261015T051414304050Z/revalidation-index.json`，SHA `05EB1402AA52864A752155C1D2DC42B5859757FC91A26FF18B04B54B45DD838B`。

最终模块 SHA `1FB3D199B063092649E8CE9B716E69884D8353AC3AD4C1623BE883722FC725ED`，入口 SHA `27B2F749396C7B0CA00F6B4599997CC82FE343D5B039A7A0D3DFD2AE14A7CAEF`，测试 SHA `D1CF66EC6A0B6B5AE01F6C96BE4225A508820B8FA5DB346DE60C64996B639179`。DLL 仍为 `D526BAEF456ED40FAB4B591B8676FECF4020D431988FE807CE223EDD883E5A13`。

后续优先完善日常使用和结果解释，再补自动保存重试与故障回退的缺失实测。inactive/COMPLETE、角色能力牌、敌人精确恢复与重编程继续不扩域。
