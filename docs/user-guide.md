# 小关重开与受限玩家恢复：使用指南

适用于当前 v0.45.0 DLL 和仓库中的只读状态工具。当前产品是单检查点研究原型：可以重开普通地牢的小关，并在通过准入时恢复部分玩家状态。敌人会按重开流程生成，敌人伤害、动作历史及完整战场快照不在承诺内。

## 日常操作

1. **确认已安装 C++ 模块。** 已安装并启用时，无需每次启动前重复安装。新环境按 [C++ 开发说明](cpp-development.md)准备已验证的游戏构建、UE4SS 和工具链；完全退出游戏后使用仓库安装脚本。开发测试夹具保持关闭，修改 DLL 后要完整重启游戏。
2. **进入普通地牢，等动画和目标选择结束。** 只在稳定的战斗状态保存。正常波次生成后会安排一次自动保存；首次观察到新稳定波次也可能触发。它不在每次操作后保存，恢复刚结束的同一波次不会立即自动覆盖检查点。
3. **手动保存用 `Ctrl+Shift+F5`。** 每次成功发布覆盖唯一检查点。想留住某一版时，退出游戏后另存整个 `Mods/QuantumCheckpoint/Checkpoint` 目录，并同时留存 Reports 和 trace 便于核对；只复制主档会丢失玩家恢复所需补充文件。
4. **用状态工具确认刚才发生了什么。** 在仓库根目录运行下面的命令，核对回执的尝试时间、结果与当前文件关系。无新回执或文件不对应时，不能把旧“成功”当作本次成功。
5. **读取用 `Ctrl+Shift+F6`。** 这会离开当前战斗状态，按检查点重开小关。重建手牌和场地时可能出现搬牌、动画；保持游戏前台，等待流程结束再出牌或点击抽牌，避免中途操作导致验证失败。
6. **恢复后查看本次恢复报告。** 核对报告时间、关卡、波次与 `routeCPayloadChecksum`，再看状态、恢复模式和实际启用的补充层。保存回执与状态工具都不能替代恢复报告。

```powershell
# 中文状态；默认游戏目录与安装脚本一致
pwsh -NoProfile -File ./scripts/Get-CheckpointStatus.ps1

# 游戏安装在其他位置
pwsh -NoProfile -File ./scripts/Get-CheckpointStatus.ps1 -GameRoot 'D:/SteamLibrary/steamapps/common/Quantum Protocol'

# 自动处理输出时使用 JSON，仍然只读
pwsh -NoProfile -File ./scripts/Get-CheckpointStatus.ps1 -Json
```

工具需要 PowerShell 7.2+，本轮实测7.6.5。退出码0只代表完成状态观察；拒绝、陈旧和依赖不完整也可以返回0。详细参数、返回值和边界见[状态工具说明](phase-46-checkpoint-status.md)。

## 怎样读结果

下表的保存结果解释以回执可识别、自洽且 `diagnosticComplete=true` 为前提。诊断不完整时，即使原记录写着某个 outcome，也不能确认实际发布结果；状态工具会明确显示不明。

| 看到的结果 | 应如何理解 |
| --- | --- |
| 保存回执 `player-bundle-committed` | 选中的玩家布局及资格依赖已提交；还要检查当前文件对应关系和可选组件，不能理解为完整玩家快照 |
| 保存回执 `main-only` | 主档已提交，玩家布局／资格未成套保存；读取时不要期待精确玩家状态 |
| 保存回执 `partial-publication` | 选中组件没有全部完成，或诊断／依赖不完整；查看部件和原因，不把它当完整保存 |
| 保存回执 `rejected-before-main` | 本次没有发布新主档；当前旧档的来源和状态仍需另行核对 |
| 当前文件 `matching-files` | 字节及已知头绑定与回执对应；这不验证游戏恢复准入，main-only 也可能得到这个结果 |
| 当前文件 `stale`、`incomplete`、`unknown` | 分别表示不对应、依赖不完整、无法确认；不要手改校验和或拼接不同次保存的文件 |
| 恢复报告 `status=passed`、`restoreMode=requested` | 本次实际请求的范围通过运行时验证；查看具体补充层状态，纯 Route C 请求也可能如此 |
| 恢复报告 `restoreMode=preflight-route-c` | 启动前已禁用不满足条件的补充层，按普通小关重开；原因在 `fallbackReason` |
| 恢复报告 `restoreMode=semantic-fallback` | 记录了一次普通重开回退尝试，也可能启动失败；仍须看这份报告最终 `status`，不能仅凭模式名认定成功 |

`retrying-route-c` 是准备回退的中间结果，后面还会产生终态报告。一次操作可能留下多份报告；不要只看到早期失败就忽略后续回退，也不要拿以前的 passed 代替本次结果。`reason`、各 `exact…Status/Reason` 和 `playerQualificationStatus/Reason` 用于定位本次启用、拒绝或未覆盖的范围。

长弓 ONCE 回放次数依据实际已用目标而定；没有已用目标时可以为0，当前已验证案例也有1或2。`fullPlayerExactRestorationProven` 始终为false，表示尚未覆盖完整玩家状态。旧档可能仍可按原有受限格式读取，但其保存时资格缺失不会被补称为已证明。

## 当前可用范围

基础路线面向已验证 Windows 游戏构建中的普通 `DUNGEON` 小关。无限模式、事件、教程、Boss 伴生及额外 Spawner 状态不作为通用支持范围。玩家精确层还需同时满足牌区、牌序、完整卡牌集合、数值、技能定义和使用资格等条件，不能仅凭卡名判断。

| 范围 | 当前限制 |
| --- | --- |
| 原生牌序、手牌、墓地、场格 | 只对通过当前布局和资格检查的来源启用；空牌区、同名副本、满手牌暂存有实测 |
| 玩家与卡牌生命、攻击、普通计数、抽牌进度 | 各层有独立边界；不是任意等级、生命修正、计数或动作历史都能恢复 |
| 基础长弓 ONCE | 已用／未用状态有受限原生回放；升级长弓、其他动态 ONCE、任意 flags 不在范围内 |
| 长弓留在场上 | 一张或恰两张基础长弓，默认数值、active=True等专用门禁；正常发动后的inactive/COMPLETE来源仍不支持 |
| 角色资源 | 支持已验证的未满充数值及许可匹配；不写许可位，不覆盖满充和能力牌生命周期 |
| 生成卡与动态生命的历史实验 | 一张额外苹果、焰火成长等曾验证底层恢复机制；当前新捕获还需通过后续增加的资格白名单，不能把历史案例当任意新来源可用 |

当前资格表包含苹果、柠檬、樱桃的基础／升级定义，`spDeckEdit@0`、基础上升涡流，以及 Q2 策略下的基础长弓。**这只是资格层允许的定义集合，不是任意牌区和使用状态组合的白名单。** 例如未知卡牌即使技能看似未使用，也不能据此获得新保存资格。详细定义见 [EffectQualificationPolicy.hpp](../cpp/QuantumCheckpoint/include/EffectQualificationPolicy.hpp)。

遇到超范围来源时，主档可能成功而玩家层被拒绝；读取时也可能禁用补充，或在精确流程失败且符合回退条件时尝试一次普通重开。以本次回执／报告为准。技能私有历史、完整 UI 状态、LEVEL 修正、独立生命调整、STORAGE／能力牌和敌人精确状态仍未覆盖；重编程路线继续搁置。

## 文件与问题定位

以下路径相对游戏的 `Quantum/Binaries/Win64`：

| 文件 | 用途 |
| --- | --- |
| `Mods/QuantumCheckpoint/Checkpoint/` | 唯一主档和配套补充文件；备份按整个目录处理 |
| `Mods/QuantumCheckpoint/Reports/route-c-capture-latest.json` | 上次成功写出的保存诊断回执，可能陈旧 |
| `Mods/QuantumCheckpoint/Reports/route-c-restore-*.json` | 各次恢复结果，按本次时间及来源核对 |
| `Mods/QuantumCheckpoint/route-c-trace.log` | 保存／恢复阶段与拒绝原因的诊断轨迹 |

如果出现问题，保留当前 Checkpoint、相关 Reports、trace 和所用 DLL 版本；先记录实际按键、时间、画面与结果。`Ctrl+F1` 可导出只读研究报告，但不是普通保存键。README 中的 Lua 快照、清空牌区和受控写入探针属于研究功能，不是日常使用流程。

本指南按当前源码、阶段40–46结果核对；文档整理没有新增游戏实测，也不扩展支持范围。
