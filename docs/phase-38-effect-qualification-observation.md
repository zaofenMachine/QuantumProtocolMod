# 第三十八阶段：技能使用资格观测与 ONCE 恢复缺口

2026-09-30。继续“小关重开＋玩家精确恢复”，重编程与敌人精确恢复仍搁置。v0.39.0 只增加 F1 的原生技能资格观测，没有增加资格保存或写回。实机已证明：普通返手保留长弓的 ONCE 已用状态，当前精确恢复会把它重建为未用，之后可以再次发动。因此，技能成员和数值一致不能代表完整玩家状态一致。

## 实现范围

新增独立 `nativeEffectQualification:` 前缀，不改变 `nativeEffects:ordered`、既有比较器、检查点格式或恢复算法。逐卡导出完整 CardInfo、原生位置、卡与技能 GUID、实例指针、有序 factory descriptor、`usedOnceRaw` 和 flags；`oncePresent`、`derivedOnceConsumed` 只是派生诊断，不代表全部发动条件。

读取受 EXE 指纹、四段原生签名、shared owner、world、位置、getId 与原生 GUID 对照约束。used 只接受 0/1，flags 只接受 0..7 的不重复成员，稀疏槽和位图有界，读取前后复核成员、身份与存储。失败输出 unavailable/readError，不能按未用或空 flags 处理。F1 是游戏线程上的同步观测，不是全局冻结或 GC 锁；Native/BlueprintPure getter 也不构成形式化零副作用证明。

独立源码审计确认：移除新增观测块、唯一 F1 调用并还原版本串后，与 `eeafdff6cec00d219d051ba7729e792a31c42912` 的源码一致（仅规范化换行）。保存、恢复、动作与持久化实现未改。

生产 DLL SHA-256：`0F514C38A637D6A26D6497886A658FF86E632F8A6CC5A6D3263AA7C27A646881`；实际构建 Mod 源码 SHA-256：`3167A2FB4671020B0960FAAC16682E9C1C151D7D1C86DEE572204AC7B6F617D7`。正式 OFF 构建 4/4 CTest，通过七个 DEV 标记的 ASCII/UTF-16 缺席检查。本阶段没有重跑 ON 构建。最初用 Windows PowerShell 5 调用构建脚本因 PowerShell 7 语法而失败，发生在配置前；失败日志和之后成功的构建日志分别保留。

## 正常来源与恢复结果

临时夹具只改牌组配置与校验和；used、flags 和运行态通过正常游戏操作形成，未直接写内存。长弓为未升级 `sakuraBow`，技能 factory 为 `player.aurora.sakura.sakurabow`。

| 观测 | Bow 位置 | used | flags | 结论 |
| --- | --- | --- | --- | --- |
| `bow-unused-hand` | HAND | 0 | [0] | 初始未用 |
| `bow-unused-field` | BACK1 | 0 | [0] | 普通出牌保持未用 |
| `bow-used-field` | BACK1 | 1 | [0] | 发动后已用，UI 显示 USED_ONCE |
| `bow-returned-hand` | HAND | 1 | [0] | 普通上升涡流返手仍已用 |
| `bow-used-unique-zones` | HAND 原生下标 2 | 1 | [0] | 最终精确保存前 |
| `bow-restored` | HAND 原生下标 2 | 0 | [0] | 重开恢复丢失已用状态 |
| `bow-restored-unused-field-confirmed` | BACK2 | 0 | [0] | 重建卡普通出牌 |
| `bow-restored-used-again` | BACK2 | 1 | [0] | 普通点击后再次发动 |

前四步核对同一 Bow GUID、完整 CardInfo、card/effect 实例和成员顺序；used 为 0→0→1→1。跨恢复按完整定义、原生牌区下标和技能顺序绑定，允许卡与技能 GUID/指针重建，不把新实例当旧实例。最后一组保留真实输入、同重建实例的 used 0→1、UI 动作与敌人离场观测，不把技能参数“4”写成实际扣除 4 HP，也不据此声称敌人精确恢复。

第一次保存 `save-bow-used` 因 Updraft 仍在 FIELD，超出受支持场地范围而未生成精确布局。清场后 `save-bow-used-clean` 又因 HAND/TRASH 同定义 Updraft 分配歧义而拒绝精确布局。两次均只完成普通主存档及适用补充，不计精确成功。正常将第二张 Updraft 也移入 TRASH 后，`save-bow-used-unique` 的 T6/HAND3 捕获独立验收通过。

恢复报告的 requested/passed 只表示现有恢复实现完成。未修改的完整比较仍有三项玩家检查失败：`playerNativeOrderedStateEqual`、`playerCardStateIncludingLocationEqual`、`playerCardRuntimeStateIgnoringLocationEqual`。原生牌序、技能成员、数值、生命与计数检查相等；UI/history 差异原样保留，不做 COMPLETE/NONE 归一化。独立资格验收结果是 `observed-qualification-mismatch`，不能写成全 25 项通过。

一次输入尝试误把苹果拖出，目录 `bow-restored-unused-field` 名称只是操作意图：该样本中 Bow 仍在 HAND，不能计入 Bow 场地正例。改正坐标后的 `...-confirmed` 才是有效场地观测。全部原始输入和截图均保留。

## 证据与验证边界

独立证据根：`F:/Project/QuantumProtoclMod.runtime-evidence/20260930-effect-qualification`。主要入口为 `validation-summary.json`、`freeze-revalidation-index.json`、`phase38-final-tooling` 与 `session-cleanup-final.json`。自然链、跨恢复资格缺口和普通再次使用以最终冻结复验结果为准；原始 F1、输入检查点副本、F6 前文件证明、trace、恢复报告、截图、构建与部署记录均按实际 DLL 绑定。

最终归档含 39 个本阶段工具／构建／审计文件，并按固定 SHA 依赖第三十七阶段的 54 文件闭包。冻结工具完成六项复验：自然链、资格损失、普通再使用、解析负例、证据链负例、原用户检查点旧范围；核对 154 个引用文件。资格损失和旧三项失败均按预期保留，不是六组玩家恢复通过。冻结 manifest SHA 为 `916E65500CB248FE3FE312DDE8EAC58B1D9455866D457EC5D789FAB343B72CFF`，复验索引 SHA 为 `606DC71AF6BB4B563854CEB2A926973B9A1D335BE0ADE3A90E5DAA26F1495EFE`。提交、推送及源码归档记录另见 `commit-and-push.json`。

自然链解析器的两组正例与二十组拒绝测试属于合成解析测试，不是额外实机样本。独立工具审查发现，初版 reuse 验收会信任前一份 loss 结论文件的自报输入清单；删除失败列表或清空清单也能误接受。修正后重新执行冻结比较器，并从原始 CP、输入副本、trace/report 复算完整 loss，核对全部派生字段和证据闭包。旧结果原样保留，仅作历史记录；修正前脚本没有单独冻结，不能宣称旧 validator SHA 对应源码仍在档。修正版另做包含完整入口篡改用例的十七项拒绝测试，并跨 Python hash seed 验证。冻结严格验收的首错由集合遍历顺序决定，故只允许同一完整失败集合内的精确诊断首错顺序不同；原三项失败、25 项比较和 UI/history 内容仍严格一致，不豁免未知错误。

最终原用户检查点在 v0.39.0 上恢复通过既有全 25 项玩家检查；它沿用原格式可证明范围，不反向证明该旧样本的 ONCE 状态。游戏已正常关闭，原 17 个检查点文件和 4 个 SaveGames 均按备份逐字节还原，主校验和 `81095982711C71E2`，临时最小化的 Genesis 窗口已还原。首次收尾因 PowerShell 自动将时间字符串转成 DateTime 导致纯预检校验不符，发生在关游戏或改文件前，失败记录 `session-cleanup.json` 保留；修正时间字面量解析后最终收尾成功。已部署的 v0.39.0 观察版保持安装。

本阶段未实现：资格持久化、ONCE 写回、动态 flags 恢复、所有技能私有状态或完整历史恢复。默认 ONCE 的 Bow 不能覆盖“动态添加 ONCE 但 used 仍为 0”的情况。下一步应先让保存/报告准确声明资格覆盖，未知状态保守拒绝精确声明；随后才做受限的已用 ONCE 原生恢复与普通再次发动阻止测试，并补未用、同名副本、真正刷新和动态 flags 对照。旧检查点不能凭重建后的默认值被补称为保存时已证明。
