# 第三十五阶段：玩家技能成员的保存与恢复门禁

日期：2026-09-20。生产版本 v0.36.0。方向仍为“小关重开 + 玩家精确恢复”。本阶段补齐技能被删除却仍获准精确保存的缺口，不恢复技能执行历史，也未增加动态基础生命恢复。代码、4/4 CTest 与实机验证已完成，最终证据归档见本阶段 validation-summary.json。

## 已确认的旧版漏洞

第三十四阶段通过正常出牌复现：露西娅 `mageLucia` 原生技能为两个，攻击/生命为 2/2；格式化删除技能并将数值设为 2/2，因此完整 CardInfo 和数值均未变化。上升涡流将其返手，再执行后排清空 FIELD 后，v0.35.0 仍写入 HAND 与 TRASH 精确补充。恢复报告为 passed，但露西娅的两个技能重新出现。完整定义相同无法证明实际技能仍然存在。

本阶段又在旧 DLL 上确认现有 FIELD 范围也受影响：普通苹果原为 2/2、一个技能；格式化删除该技能后，苹果仍留在 BACK0，格式化进入 TRASH。v0.35.0 成功捕获 field schema 7，恢复报告 passed，苹果技能却从 0 恢复为 1。这是既有 plain-fruit FIELD 的漏检，不依赖扩展露西娅 FIELD 支持。

牌组起点为临时合成组合，技能删除、回手与送入 TRASH 均由正常 UI 操作和原生效果完成，没有直接注入技能数量。两例均保留原始失败比较，不能计作精确技能恢复通过。

| 证据 | 路径 |
| --- | --- |
| 露西娅 HAND 复现、前后清单与旧版恢复 | `F:/Project/QuantumProtoclMod.runtime-evidence/20260920-native-effect-membership`；`natural-effect-removal-reproduction.json`、`observer-lucia-hand-clean-field`、`checkpoint-removed-hand-observer`、`observer-removed-hand-restored`、`removed-hand-comparison.json` |
| 苹果 FIELD 复现、前后清单与旧版恢复 | `F:/Project/QuantumProtoclMod.runtime-evidence/20260920-effect-membership-guards`；`apple-field-before-observer`、`apple-field-removed-observer`、`checkpoint-removed-field-observer`、`apple-field-restored-observer`、`apple-field-observer-comparison.json` |

## 两项独立保证

**保存时检查。** 新捕获在准备玩家精确补充前，检查所有玩家 DECK、HAND、TRASH、FIELD。每张卡的实际原生技能工厂键必须与完整 CardInfo 中当前升级级别的 `effects` 数组逐项相同，顺序与重复数量均保留。未知空工厂键拒绝；定义和原生数组均为空则合法。PENDING 不在支持范围。检查不依赖 UI overlay、CardFace 显示缓存，也不因数值相同跳过技能检查。

任一玩家卡读取失败或成员不匹配，本次不发布玩家布局、HAND 动态状态、回合进度或角色充能补充；普通 Route C 与有效未来刷怪计划仍可保存。因此 F5 保存主文件成功不等于所有精确层都已保存，拒绝原因见 trace。

**恢复时检查。** 只要实际选用了玩家精确布局，新旧版本都检查当前原生技能与当前完整定义是否匹配。第一次检查位于原生启动稳定后、首个玩家暂存或恢复写入前；每张 FIELD 目标在临时技能抑制前，以及恢复原始技能数量后分别检查。全部布局完成后、HAND 数值恢复前再次全量检查，并在最终稳定窗口的每次恢复更新中重复检查。最终 passed 要求本项为 `verified-native-definition-membership`。

全量检查只能在受控 FIELD 技能抑制之外执行，不能把临时 count=0 当作真实技能删除，也不能为掩盖不匹配而补建技能。异常沿用既有一次普通 Route C 回退，原失败报告保留。异常清理原语只负责释放临时抑制、恢复原来的数量；定义验证位于正常恢复调用方，避免读取失败打断清理。

这两项保证只覆盖技能成员。相同工厂键不能证明效果的执行历史、私有状态、动态 flags 或资格状态相同；`COMPLETE/NONE` 等历史差异仍按原始比较保留。旧存档没有保存时证明时，即使重建后的成员符合定义，也不能据此证明保存前没有删过技能。

## 布局版本与报告

不新增技能 sidecar 或持久化成员列表。完整 CardInfo 已包含所选升级的工厂键序列；新布局版本表示捕获时已验证实际成员符合该序列。

| 布局 | 新写入版本 | 保存时成员证明最低版本 | 原 HAND 依赖最低版本 |
| --- | --- | --- | --- |
| zones | 4 | 4 | 3 |
| trash | 5 | 5 | 4 |
| field | 8 | 8 | 7 |

三个固定 `EffectMembershipSchemaVersion` 常量与可继续演进的最新版本常量独立。版本本身已进入原有 payload checksum，单独修改版本不会通过校验；旧版读取、字段、HASH 规则保持原语义。HAND schema 2 以及 `playerHandHealthChecksum` 绑定关系不变。

报告新增：

- `exactPlayerEffectMembershipStatus`：`pending`、`verified-native-definition-membership`、`failed` 或 `not-applicable-no-player-layout`。
- `exactPlayerEffectMembershipReason`：当前检查结果与失败原因。
- `exactPlayerEffectMembershipSavedProof`：`attested-by-layout-schema`、`legacy-unknown` 或 `not-applicable-no-player-layout`。

保存时证明根据最终实际选中的布局判断，优先级为 FIELD、TRASH、zones；依赖校验或启动预检已禁用布局时为不适用。较低优先级的新文件不能替较高优先级的旧布局提供证明，纯语义回退也不继承先前的 attested 状态。旧版允许继续使用，明确报告 `legacy-unknown`，不会自动升级为“保存时已验证”。

## 只读原生依据

原生卡 `+1B8/+1C0/+1C4` 保存共享技能数组及数量/容量，元素为 16 字节共享指针对。成员读取沿用游戏线程、EXE 指纹、当前 world、UObject 生命周期、引擎及共享所有权检查，数量限 0–64、容量不超过 256。effect `+58` 的有界 UTF-16 FString 保存原始工厂键；构造路径按定义数组顺序逐个追加，没有按技能类型重新排序。

typed reader 同时供 F1 使用，保持既有 `nativeEffects:ordered` 的 `{tag,type,factoryKey}` 格式。新增本进程身份检查：effect 的 owner GUID（`+80`）必须等于 native card GUID（`+18`），对应卡 GUID Getter `0xE26AD0` 与构造写入 `0xDF6CB6` 均有字节签名守卫。不把指针或 GUID 当作跨进程语义状态，也不调用会复制数组或改变引用计数的 Getter。

## 防止旧补充文件被重新关联

Route C 捕获时间只有秒级精度。同秒重复捕获若主语义内容相同，可能产生相同 Route C checksum；仅跳过新补充写入，旧精确文件仍可能继续关联到新主文件。

新实现完成全部 prepare，并将主 payload 序列化、检查大小后，在提交新 `route-c.json` 前，显式失效七类补充及各自 `.bak`，合计 14 个固定路径：未来刷怪、zones、trash、field、HAND、回合进度、角色充能。使用非递归、带 `error_code` 检查的删除；不存在视为成功，任一错误立即中止新主文件提交，诊断只使用固定文件名和数字错误码。随后才写新主文件与本次已验证的补充，optional 写入失败也不能复活旧文件。

以下路径均相对于 `Mods/QuantumCheckpoint/Checkpoint`，也是本次失效操作的完整范围：

| 主补充文件 | 对应备份 |
| --- | --- |
| `route-c-exact-spawn-plan.json` | `route-c-exact-spawn-plan.json.bak` |
| `route-c-exact-player-zones.json` | `route-c-exact-player-zones.json.bak` |
| `route-c-exact-player-trash.json` | `route-c-exact-player-trash.json.bak` |
| `route-c-exact-player-field.json` | `route-c-exact-player-field.json.bak` |
| `route-c-exact-player-hand-health.json` | `route-c-exact-player-hand-health.json.bak` |
| `route-c-exact-turn-progress.json` | `route-c-exact-turn-progress.json.bak` |
| `route-c-exact-character-charge.json` | `route-c-exact-character-charge.json.bak` |

部分旧夹具仍带有废弃的 `route-c-exact-battle-turn.json` 及 `.bak`，当前运行时不读取它们，本操作也不删除它们。这里没有按 `exact*.bak` 通配符清空任意文件的承诺；`.tmp` 同样不是可加载的补充输入。

这是有意的失败代价：若删除进行到一半失败，旧语义主文件保持不变，已经删除的精确补充不会回滚，旧精确恢复可能降级；若全部删除后新主文件写入失败，旧语义主文件仍在，但旧精确补充已失效。它不是多文件原子事务。主文件及其备份不属于这 14 个删除目标。

关键 trace：`capture.player-effect-membership.verified/rejected`、`capture.old-exact-supplements.invalidated/invalidation-failed`、`restore.player-effect-membership.startup-verified/failed`。

## 验证结果

生产构建关闭开发夹具，4/4 CTest 已通过。持久化测试涵盖新旧版本往返、版本升降但未重算校验和的拒绝、无新 JSON 字段、既有 HAND 依赖，以及 field schema 8 暂存规划。

当前部署 DLL SHA-256：

```text
DC2239856598C231E1FE41107E879723801A80E799F28708F574A26D40D3BABD
```

部署证据为本阶段目录下 `guard-deployment.json`，`runtimeTestFixturesEnabled=false`。独立只读代码审阅见 `runtime/phase35-review-effect-membership.md`，冻结源码未发现阻塞问题；这份审阅不代替实机证据。本阶段共 14 组精确恢复回归，另含原用户检查点恢复；原始敌人差异保留，不计入玩家精确覆盖。

| 实机项目 | 当前结果 |
| --- | --- |
| 旧 zones、trash、field 检查点恢复 | 三例已通过；恢复端原生成员符合定义，既有严格玩家检查与未来刷怪计划匹配，保存时证明均为 `legacy-unknown`；见 `legacy-zones`、`legacy-trash`、`legacy-field` |
| 旧布局恢复后正常 F5 再保存 | 三例已通过，分别产生 Z4/T5/F8，14 个受支持路径范围内没有旧补充备份残留；见 `save-legacy-zones`、`save-legacy-trash`、`save-legacy-field` |
| 新 Z4/T5/F8 再恢复 | 三例已通过；保存时证明均为 `attested-by-layout-schema`，严格玩家与未来刷怪比较、跨清单原生技能成员比较及恢复端定义一致性均通过；见 `attested-zones`、`attested-trash`、`attested-field` |
| 删除实验起点回归 | 已通过旧布局严格恢复，已有 native 前后观测，成员逐项相等；保存时证明仍为 `legacy-unknown`；见 `removal-start-guard` |
| 新 F8 新进程恢复 | 已通过，与上述新布局同样检查；见 `attested-field-fresh` |
| 生成卡、空 HAND 回归 | 六例已通过；生成苹果留场、生成卡退场后的空 HAND、全部卡在 TRASH，各覆盖旧版及新 attested 布局；见 `legacy/attested-generated-field`、`legacy/attested-generated-empty`、`legacy/attested-all-trash` |
| 新 DLL 对真实 FIELD 技能删除的捕获拒绝 | 已通过；自然格式化后的苹果 expected=1、observed=0，location=3；仅保留主语义文件及其备份、未来刷怪补充；见 `apple-field-removed-guard`、`reject-removed-field` |
| 新 DLL 对真实 HAND 技能删除的捕获拒绝 | 已通过；无技能苹果返手后 expected=1、observed=0，location=0；同样不发布玩家、HAND、回合或充能补充；见 `apple-hand-removed-guard`、`reject-removed-hand` |
| 拒绝精确捕获后的主文件恢复 | 已通过普通 Route C requested restore，玩家精确层 unavailable，live status 与 saved proof 均为 `not-applicable-no-player-layout`；见 `rejected-hand-semantic-restore` |
| 锁定旧补充文件，验证删除失败且主文件不变 | 已通过；首个旧补充以不共享删除的方式锁定，捕获中止，主文件及全部检查点文件字节未变；见 `save-invalidation-locked`。该例不代表中途已删除部分文件的错误分支也保留全部旧文件 |
| 最终稳定窗口正常操作导致失败与单次回退 | 已通过；HAND 健康/计数验证后正常打出弹幕，原尝试因 HAND 顺序/定义不匹配失败，恰好一次普通 Route C 回退通过；saved proof 从 attested 重置为不适用，见 `stability-interference`。该例是普通布局扰动，不是隔离的技能成员漂移测试 |
| 原用户检查点恢复、最终文件清单与归档 | requested restore 已通过，17 文件与原备份逐字节一致，主 checksum `81095982711C71E2`；见 `original-restored-final`、`original-checkpoint-restored.json`。最终源码/构建/测试/报告由 `artifact-manifest.json` 列出 |

六份 `legacy-*` 基线（普通三区布局及生成卡/空 HAND）没有原生成员观测字段，因此 `nativeCrossInventoryMembershipAvailable=false`、对应 equality 为 null；不能把恢复端符合定义描述成已经与旧保存端逐项比较通过。新 Z4/T5/F8 及新进程 F8 的前后清单都有覆盖，跨清单 availability/equality 均为 true。

第三十三阶段同进程 Cherry 观测缺口在第三十四阶段五组暖运行中未复现，原因仍未知；本阶段成员门禁不能据此宣布已修复该问题。敌人仍按小关重开，重编程重开仍搁置；后续动态基础生命需另立真实样本与受限恢复验证。

本阶段保存夹具的备份检查曾误把已废弃 battle-turn 文件算作现行补充，已收窄为明确的七类文件并对原始 capture 离线重验。原用户检查点的前后比较又暴露 FIELD 枚举顺序误报：同名苹果位于相同场格且成员相同，仅对象枚举顺序不同。比较改为按场格规范排序，并用枚举换序应相等、同场格成员变化应不等的样本复核；修正后的比较脚本在 PowerShell 7 和 Windows PowerShell 5.1 各通过 51 项检查，15 组本阶段前后清单已重新比较通过玩家范围。原误报保留为 `comparison-before-field-sort-fix.json`，不删除失败证据。
