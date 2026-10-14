# 第四十阶段：基础长弓的原生 ONCE 回放

2026-10-15。继续 Route C 小关重开＋有界玩家恢复。这里的“切片 B”是第三十九阶段资格门禁之后的资格策略，不是重编程 Route B；没有加入敌人精确恢复。v0.41.0 只扩展已审阅基础长弓的原生 flags/used 范围，不代表完整玩家状态恢复。

## 资格来源与一次回放

新策略为 `flags-baseline-bow-once-v2`，资格证明 schema2（Q2），必需布局版本 Z7/T8/F11。第三十九阶段 `flags-baseline-v1`、Q1 和 Z6/T7/F10 的含义及旧 checksum 保持独立。证明绑定主存档、EXE、捕获时间、所选布局 checksum，以及全部玩家 D/H/T/F 的位置、完整 CardInfo 和有序技能 descriptor。缺失或错误绑定必需证明时，拒绝依赖玩家层，普通 Route C 仍可用；旧档没有保存时资格来源，只能声明 `legacy-bounded`／`unknown-saved-state`。

唯一新增 ONCE profile 是未升级 `sakuraBow`：单个技能、factory `player.aurora.sakura.sakurabow`、tag `sakuraBow`、type IGNITION1、flags 精确 `[0]`，并核对固定 vtable、参数及完整定义。source used0 不写；source used1 要求新生实例为0，完成布局和数值恢复、解除临时 effect 抑制、OPEN／无提示／原生队列 settled 后，调用一次 `E52E80(effect,true)`。完整228字节签名、当前 owner／GUID／ordinal、当前 used0、世界弱身份和同步删除 epoch 受守卫；前后全量快照只允许指定目标 used0→1。

不调用 false，不直接写 used 或 flags，不复制旧实例、不创建工厂探针。一次尝试标记先于调用；异常或部分回放后的非预期差异失败并走既有单次语义回退，不能 reset／重试。完成后的稳定窗口只读。部分失败处理已做源码审查，但本轮没有在实机注入 setter 部分失败，不把正例替代该负例。

资格能识别基础长弓，不等于其任意区域布局可保存。原 FIELD／特殊牌跨区／重复身份分配守卫没有放开；长弓本身不属于受支持的 plain FIELD 卡。升级长弓、其他 ONCE 工厂、未知 profile、额外／移除 flags 均不因此获得支持。

## 实际原生资格验收

以下六个 B 案例的最终独立 `audit-final.json` 均通过25项必需玩家／上下文检查；另外3项比较仍按原值保留，不把敌人／runtime IDs／semantic aggregate 差异归一化成相等。

| 案例 | 保存布局与长弓位置 | 实际回放次数 |
| --- | --- | --- |
| `single-bow-unused-restored` | 未用基础长弓 | 0 |
| `single-bow-used-restored` | T8，已用长弓在 HAND | 1 |
| `double-bow-mixed-restored` | T8，HAND 下标0未用、下标2已用，中间为 Apple | 1 |
| `double-bow-both-used-restored` | T8，HAND 下标0、2均已用 | 2 |
| `single-bow-used-field-restored` | F11，长弓在 HAND 下标0，Apple 在 FIELD；不是长弓 FIELD 保存 | 1 |
| `single-bow-used-trash-restored` | T8，已用长弓在 TRASH 下标3 | 1 |

同名混合样本只回放下标2，首张未用副本保持0；双已用样本 trace 分别绑定下标0、2的重建 card/effect 身份，各0→1一次。跨恢复按位置、完整定义与 ordinal 绑定，允许 GUID／指针重建，不把新实例当旧实例。HAND 已用来源经普通 Updraft 返手及区域清理形成，没有靠直接写 used／flags 生产正例。

三个实际 Q2 依赖负例 `q2-missing-restored`、`q2-a-policy-mismatch-restored`、`q2-bad-checksum-restored` 均独立验收为 `verified-rejected`：资格必需层拒绝、回放0，普通语义恢复保留。它们不要求原玩家布局比较全部相等，也不能计入资格恢复正例。

本轮未取得正常产生的动态 ONCE 对照；现有受支持 profiles 不能构造该来源而又保持同一支持范围。未知工厂／flags 的纯解析拒绝测试不等于动态 ONCE 实机证明，未借此扩展 Quarter／Idol 等工厂。

## 普通操作与真正刷新对照

单长弓及双长弓自然链确认同 card/effect 实例 flags `[0]` 下 used0→1，普通 Updraft 返手仍为1。重建已用长弓正常出牌后，实际导出的 widget blockers 为 `USED_ONCE`，截图有红X／used 提示，采样 used1保持1；重建未用副本普通发动后同实例 used0→1，UI NONE→COMPLETE／USED_ONCE。

这些是输入关联的端点证据。输入文件明确 `gameplayAppliedProvenByInput=false`；F1 未导出长弓 `canClick` 或连续原生动作队列，没有覆盖整个点击区间的 accepted-handler／dispatch stream。因此不声称“整个点击期间没有 accepted dispatch”或 ONCE 是唯一拒绝原因。true setter 本身没有 false 分支的 UI 刷新通知；原生资格与普通操作后 UI 结论分别保留，NONE／COMPLETE、并存 blockers 和游戏变化不做规范化。

独立 Shide 对照从 main-only 语义起点，经正常游戏操作观察同一 Bow used0→1→返手0→重新出牌0→再发动1，flags／完整定义／原生 effect 身份保持，USED_ONCE 随刷新消失并在再发动后出现。Shide 自身不在资格 profile；这是游戏原生刷新对照，不是 Shide 或 Q2 检查点成功。

首次双已用尝试 `double-both-used-field` 和首次 Shide 尝试 `shide-bow-used-field` 均出现 Bow 原生 effect／qualification 数组为空。原始证据及 exclusive attempt outcome 保留为不合格来源；空数组不是 used0／1，不能声称已用或刷新。战场变化的确切原因未知，不追加敌人因果或伤害量结论。

## A 兼容性与原档回归

旧进程中的 `compat-a-zones`／`trash`／`field` 各仅24／25检查通过，整组失败；唯一必需失败为 `characterResourceEqual`：abilityOk False→True。charge0／requirement6、Q1原生资格验证成立，B回放不适用且次数0；trace 没有 charge apply 或 B replay。原始失败、独立验收拒绝记录均保留，不能豁免为成功。

实际编译 A491D 与 B455F 的完整 `update_exact_character_charge` 函数文本相同，SHA256 `F44442461C439BD37A3E104030D5EF3729A376FBC6B9B44FF82422BF1498D8D1`。已有 charge 层没有保存／恢复 abilityOk；当前差异不能归因为新增 Bow setter。历史充满后的状态残留只是候选解释，未证明 getter 的完整物理语义或因果链。

正常关闭再启动后，新 PID73140 的三项 `compat-a-*-fresh` 均通过25／25及独立 Q1 验收，B回放0。这支持明确区分旧会话失败与新进程兼容结果，不抹去前者。`final-original/audit-final.json` 已通过原用户检查点25项旧范围检查，资格为 legacy-bounded／unknown-saved-state；不反向证明旧存档的 ONCE 状态。

## 构建、冻结证据与会话收尾

正式 OFF 构建4／4 CTest、0失败，七个生产 DEV 标记的 ASCII／UTF-16 缺席检查通过。v0.41.0 DLL SHA256：`455F5575B3DC86A6B89E6A838511A144764496725368BF706F6344C7484AEE5D`；实际构建 Mod 源码 SHA256：`78D2EB1EBFA7C38AAF5A2449C64E7719CFD094BB6E3765ED75EF9DF8182F1D96`。固定 EXE 大小82718720，SHA256 `0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6`。没有把本轮 OFF 结果写成 ON 构建通过。

独立证据集为 `QuantumProtoclMod.runtime-evidence/20261015-bow-once-replay`，保留 build-01 实际源码／DLL、真正恢复前输入副本及三哈希证明、原 F1／PNG／输入记录、trace／raw report、最终 audit 和所有失败尝试。逐链审查见 runtime 的单 Bow、双 Bow、双目标和 Shide review；readiness 差异另见 `phase40-ability-readiness-gap-review.md`。

本轮启动前 fresh backup 共22文件：17个原检查点、4个 SaveGames、1个此前 DLL。最终69个工具文件冻结，固定1422个外部文件；`phase40-frozen-revalidation-20261014T211550805192Z/revalidation-index.json` 的31项复验通过，包含13项独立验收、两个合成自测和16组原比较复算。复算保留三个旧会话24/25失败，不能把31项复验通过解释成31次游戏恢复成功。工具 manifest SHA256 为 `5BC8E42A283C4E34C5019329B6AD619A0D3A034FA4B3411EB3A6C756A7F3B225`。

`validation-summary.json` 和 `session-cleanup.json` 已完成：唯一验证游戏进程正常退出，其父 launcher 自然退出，未强杀；先归档清理前状态，再按本轮 fresh backup 逐字节还原17个检查点和4个 SaveGames。原主checksum `81095982711C71E2`，新455F DLL保留，清理完成时游戏已关闭。summary SHA256 `F869573CCB2BE9620C84D4BA8140728433EC5B8EEF35DEF0AE263AB0A8AC974E`；cleanup SHA256 `A1F0E694A7D275F354D52EED953C233DD369C74796F4B3E4EAAC3A0740DA544A`。后续独立阶段可能再次启动游戏，以 WIP 最新入口为准。

A冻结归档原57项的长度与SHA仍全部一致；发现12个后生成的Python缓存文件，独立记录于 `phase39-derived-cache-inventory.json`，不声称它们属于原A冻结清单。B固定当前完整外部集合，复验禁用字节码生成，未删除或改写A证据。这套复验依赖固定的本机绝对路径，不是可迁移工具包。

所有报告保持 `fullPlayerExactRestorationProven=false`。已证明的是限定来源与布局下的基础长弓原生资格范围，未证明完整技能私有状态、UNIQUE／GUNIQUE、完整执行／UI历史、全点击区间动作缺席、敌人精确恢复或重编程。
