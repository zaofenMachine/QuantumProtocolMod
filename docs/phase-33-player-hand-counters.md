# 第三十三阶段：手牌 generic 计数恢复

日期：2026-09-20（Asia/Shanghai）。版本：v0.34.0。方向继续为“小关重开 + 玩家精确恢复”。本阶段实现与受限范围的最终验收已完成，效果动作历史仍明确不恢复。

## 本轮补齐的缺口

第三十二阶段已经取得真实场外计数样本：弹幕 `mdvRocket` 留在 HAND，正常放置闪光弹 `genericFlashbang`，闪光弹的 INIT 自身摧毁使弹幕的 generic counter 从 0 变为 1，special map 仍为空。v0.33.0 对该状态正确拒绝玩家精确布局，只保留普通 Route C。本阶段在原有严格门禁中增加 HAND generic 计数切片。

原生证据区分了“进入墓地”与“被摧毁”：Rocket 条件 RVA `0xFA5B00` 只接受源位置 FIELD、move type 为 `DESTROYED(2)` 或 `DESTROYED_COMBAT(3)`；普通 `EXECUTE(5)` 不满足。Flashbang 的 INIT resolve `0xF64680` 经 `0xE35A10` 排队自身移至 TRASH，并明确使用 `DESTROYED(2)`。Rocket resolve `0xFAE010` 调 `0xE0E710` 增加 generic counter；该效果没有 special-counter 标签，`mdvRocket` 是效果标签。

## 持久化与兼容契约

继续使用 `route-c-exact-player-hand-health.json` 和 kind `route-c-exact-player-hand-health`，新捕获写 schema 2。已有 schema 1 仍可读取，不为旧文件虚构计数恢复能力。

| 项目 | schema 1 | schema 2 |
| --- | --- | --- |
| 完整 HAND 实例与原生顺序 | 保留 | 保留 |
| `playerHandHealthStates` | 原有 HEALTH 契约 | 原有 HEALTH 契约不变 |
| `playerHandCounterStates` | 不允许出现 | 必填，与每个 HAND index 一一对应 |
| generic counter | 运行时要求默认 0 | 每张 0–256 |
| special counter | 必须为空 | 必须为空，包括显式的零值条目也不支持 |

新增 C++ 成员 `player_hand_counter_states`，复用已有 `PlayerCounterState` 的规范序列格式。例如两张手牌的 `1;0` 表示第一张 generic=1、第二张 generic=0；同名同升级卡仍按原生下标分别保存，不能合并为多重集合。范围沿用 `PlayerCounterRestoreMaximum=256`，不扩大原生计数动作的有界恢复能力。HAND 仍为 1–7 张；空 HAND 不写此补充。

schema 2 在旧负载校验字段序列末尾追加 `playerHandCounterStates`。schema 1 的校验和及序列化字节保持原语义；旧 JSON 偷带新键会被拒绝，schema 1 对象中的新成员非空也无法通过校验。读取只接受版本 1 或 2，未知版本拒绝。数量不匹配、负值、超界值、special 条目、非规范表示、缺键和损坏负载均拒绝。

zones schema 3、trash schema 4、field schema 7 保持不变。既有 `playerHandHealthChecksum` 绑定完整 HAND sidecar 的 payload checksum，因此同时绑定 schema 2 的生命和计数。HAND 文件仍绑定 Route C 负载、EXE 指纹、来源小关、波次和完整 HAND 文本；同名卡计数记录互换会改变校验和，旧布局依赖不会接受这一变化。捕获先写 sidecar，成功后才发布依赖布局；缺失、损坏或错链继续在启动前禁用关联玩家精确布局、回合及角色充能层，普通 Route C 保留。

## 原生恢复与稳定性检查

全部牌区移动、手牌暂存、场上放置及其恢复完成后，才进入 HAND 动态状态恢复。共享同一组固定的 native card/state 目标，生命与计数各自维护已排队动作的预期状态。

首个 HAND HEALTH 动作前，先检查全部 HAND 的 fresh generic=0、special 为空，并检查 fresh 生命为默认状态；不把启动时意外获得的计数当成恢复结果。随后先逐条重放 HEALTH，再按 HAND index 使用已有原生 generic 动作 RVA `0xE0E710`。HAND 动作门禁只允许空 special 标签、非负数量、limit=-1 和已核实的 HAND 位置，数值仍受 256 上限限制。

每次更新都检查全部 HAND 的顺序、完整定义、card/state 双指针、生命与计数预期状态。排队只改变预期值并返回等待，不立即标记完成；下一次真实读取匹配后才继续。尚未执行的动作可以保留排队前值，最多等待 8 秒；等待期间仍检查其他手牌，不忽略旁边卡牌的变化。schema 1 在整个过程持续要求 generic=0。

最终 3 秒稳定窗口继续经过相同验证。已验证的计数若发生变化，直接失败，不能重复补写掩盖漂移。部分写入后失败沿用既有一次普通 Route C 回退；最终 `passed` 要求 schema 2 的计数状态为 `verified-native-counters`。

报告新增 `exactPlayerHandCounterStatus` 和 `exactPlayerHandCounterReason`。旧 schema 1 保留 `legacy-unavailable`，并说明只允许默认零计数；schema 2 单独报告计数恢复进度与结果。`playerOffFieldStatisticsScope` 明确区分这两种范围。

## 范围与泛化依据

HAND 本轮支持非负 HEALTH 修正、current HP=max，以及 generic 0–256、special 为空。DECK/TRASH 继续要求默认数值，包括 generic=0。已有卡定义、基础攻击/生命、等级、默认倒计时及 active 检查不放宽；场外 ATTACK 修正、LEVEL 修正、special 计数和更多非默认状态仍会拒绝相应精确捕获。

生产恢复使用通用 native generic action 和统一数值校验，没有按 `mdvRocket` 卡名开通的分支。这是实现覆盖其他相同原生计数结构的依据，但实机自然计数样本目前仅来自 Rocket 的 0、1、4。0–256 是保存与恢复契约的有界范围，不表示每个数值或所有卡牌都已完成实机验证，也不代表恢复了卡牌效果/动作历史。

敌人继续按小关重开。下文观察敌方 HP 的变化，只用于确认正常打出 Rocket 会消费其计数并执行原生技能，不扩展敌人精确恢复承诺。重编程重开路线仍未纳入本阶段。

## 当前构建与已观察证据

生产 DLL v0.34.0 关闭开发夹具（`runtimeTestFixturesEnabled=false`），SHA-256：

```text
42F6F6661E4FA6E92FCEC03614F580529BD860CB62CA71544E3F3E58C033D3E1
```

4/4 CTest 已通过。持久化测试包含 schema 1 固定字节与校验和、schema 2 往返与追加字段校验和、同名手牌计数交换、既有布局依赖绑定、0/256 边界，以及负值、超界、special、错数量、缺键、类型错误、非规范格式与旧版本字段注入拒绝。部署记录为 `20260920-041402-950`，证据见 `deployment-final.json`。

实机证据目录为仓库外 `QuantumProtoclMod.runtime-evidence/20260920-hand-counters`。本记录写入时确认的事实如下：

- `legacy-zero`：旧 HAND schema 1、generic=0 的 zones 检查点恢复通过，报告 HAND HEALTH 为 `verified-native-health`、HAND counter 为 `legacy-unavailable`。随后再次保存得到新 schema 2；`resave-checks.jsonl` 记录 permanent deck 相同、HAND 依赖有效。恢复报告、比较和库存分别为 `report-legacy-zero.json`、`comparison-legacy-zero.json`、`after-legacy-zero.json`。
- `control-charged.json` → `control-played.json`：正常 Flashbang 自毁使 HAND Rocket 获得 generic=1；正常打出 Rocket 后计数变为 0，场上四个敌人各损失 1 HP。`control-play-validation.json` 已记录该自然行为验证通过。这是正常玩法对照，不单凭它宣称 schema 2 恢复通过。
- `combined-control-charged.json` → `combined-control-played.json`：同一张 HAND Rocket 的 base HP=1、HP/max=2，带 `mageOctavia_init` HEALTH +1，同时 generic=1。正常打出后 HP 增益保留、计数消费为 0，四个敌人各损失 1 HP；`combined-control-play-validation.json` 已记录通过。此样本证明两种动态状态可自然共存。
- `four-control-charged.json`：通过正常放置四张 Flashbang 并让其 INIT 自毁，HAND Rocket 获得 generic=4。该文件记录了更高计数的自然来源；其恢复及后续行为验收应以随后实际产生的报告为准。
- `one-fresh`：schema 2、generic=1 的新进程恢复，生产报告 `report-one-fresh.json` 为 `passed`，HAND HEALTH 与 counter 分别为 `verified-native-health`、`verified-native-counters`。原始比较中的玩家原生属性、生命、等级、计数、区域顺序和完整实例均相等，但完整 card state 与有序动态状态并不相等，原因见下节。最初严格验收脚本因此未通过，不能把这次结果写成“完整玩家状态相等”。

这些牌组起点是可丢弃的 **synthetic 临时组合**，由游戏已有真实卡定义构成，不代表用户原牌组天然拥有这些卡。`fixture-setup.json` 记录 combined 与 four 设置；计数和生命数值均由正常 UI 出牌、选择目标及原生效果产生，没有直接注入动态数值，也没有改写 permanent save 或解锁。

## 新进程样本暴露的效果历史差异

`comparison-one-fresh.json` 保留原始比较结果：`playerNativeStatisticsEqual`、`playerNativeCountersEqual`、`playerNativeHealthEqual`、`playerNativeLevelEqual`、原生牌区顺序和完整实例均为 true；`playerNativeOrderedStateEqual`、`playerCardStateIncludingLocationEqual`、`playerCardRuntimeStateIgnoringLocationEqual` 为 false。玩家差异定位到两处原生效果执行状态：

| 卡牌与区域 | 保存前 | 重开恢复后 |
| --- | --- | --- |
| `mdvRocket`，HAND | `TRIGGER//COMPLETE/False,INIT//NONE/False` | `TRIGGER//NONE/False,INIT//NONE/False` |
| `genericFlashbang`，TRASH | `INIT//COMPLETE/False,QUICK//NONE/False` | `INIT//NONE/False,QUICK//NONE/False` |

原来的技能已经执行过，新小关实例的对应效果历史回到 NONE；数值恢复没有重演整段技能历史。生产报告已有 `playerEffectActionHistoryRestored=false`，scope 也明确排除此项。原始完整状态比较保持 false，严格脚本首次失败如实保留，不能将其改写为全量相等或当作所有效果历史均可忽略的依据。敌方还有小关重开预期内的差异。

专项脚本 `validate-hand-counter-history.py` 已验证这一已知差异：只允许上述两个卡牌标签、对应区域和具体效果项的 `COMPLETE → NONE`，完整有序数组保序，多重集合按完整状态和数量核对，其余变化一律拒绝。全局比较原值保持不变。21 项明确标注的合成自测覆盖数值、牌序、修正、flags、未知卡、数量聚合及零历史差异；这些自测不是实机恢复证据。

## 最终验收

最终 DLL 未再改动。13 组脚本验收与独立的新进程 `one-fresh` 合计 14 组恢复完成：旧/新默认 HAND、计数 1 的新进程和再保存、同一卡牌 HEALTH +1 与计数 1、混合 FIELD、自然计数 4、旧 HAND HEALTH、旧/新生成卡场地与空场墓地、全部在墓地且空 HAND，以及原有 FIELD 攻击/生命/generic/special 组合。`cases.jsonl` 的 13 条记录和独立 `report-one-fresh.json` 保留各次结果。

`one-fresh`、`combined`、`four` 的原始完整状态比较仍为 false，专项验证确认只有前述两项效果历史变化；其余数值、原生位置、完整定义和持久上下文均相同。`legacy-field-counters` 的旧基线没有原生 ID 顺序字段，对应覆盖仍为未知，未冒称完整有序比较通过。其余正式样本完整玩家状态相同。

一次同进程零计数回归 `zero` 的 DECK 樱桃效果显示字段为空、card face 观测为 -1，完整有序观测正确标记不可用，严格脚本未通过。追加 F1 的 `zero-later` 仍不完整；原因尚未确认。相同检查点在新进程重测为 `zero-fresh`，全部比较零差异通过。原始不完整观测与失败记录保留，不作为效果历史豁免，也不计入通过数量。

恢复后正常打出 Rocket 的三组实际行为检查均通过：generic 1→0，原有生命增益保留，当前场上每个敌人各掉 1 HP。`one` 与 `mixed` 的正常出牌对照中，完整玩家状态与未来刷怪计划均相同。`combined` 的旧正常出牌对照来自独立生成的小关起点，玩家状态相同，但未来刷怪计划不同，因此明确不作为同一检查点的世界状态对照；同一次实际出牌前后的计数/生命/伤害验证仍通过。

三种依赖故障均在启动前转为 `preflight-route-c`：整个 HAND 文件缺失（含备份）、schema 2 缺计数字段、内部重新校验的计数变化但布局仍引用旧负载。均未排队 HAND HEALTH/counter 动作，未进入写后失败回退；依赖的玩家布局、回合和充能层停用，报告可按严格 UTF-8 解析。

计数恢复已验证且进入最终稳定窗口后，通过正常 UI 打出手牌 Rocket，原恢复发现状态改变，只安排一次纯 Route C 回退。记录为一份 `retrying-route-c` 与一份 `semantic-fallback / passed`，没有重复补写。

原始用户检查点另行恢复，检查点目录所有文件与本轮开始备份按字节一致。最终构建、部署、源码提交/推送、原始库存与报告、纯测试、运行脚本和文件指纹见本阶段证据目录的 `validation-summary.json` 与 `artifact-manifest.json`。

下一项先补原生效果列表观测，区分同进程樱桃缺口是原生效果缺失还是显示初始化问题，并核实效果删除是否会绕过 CardInfo 检查；之后依据本地原生研究推进 HAND 动态基础生命：`mageSharedAdder` 使用 SET_BASE_STATS 改写 baseHP/currentHP，并非独立 maxHP adjustment。先用真实出牌与回手复现，再决定受限恢复实现；HAND ATTACK、special、STORAGE 与任意效果历史不会为数值对称而提前放开。
