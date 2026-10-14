# 第三十九阶段 A：玩家资格默认来源门禁与 Q1

2026-10-15，v0.40.0。方向仍为“小关重开＋受限玩家恢复”，重编程路线及敌人精确恢复继续搁置。本切片收紧新保存的玩家精确层：只有逐卡、逐技能符合已审阅默认 flags profile 且 used=0 的来源，才能保存新版布局与 Q1，并在重建后完成独立资格复核。没有增加 ONCE 写回、flags 修改或全部技能历史恢复。

### 本阶段解决的问题

v0.39.0 的只读观测已确认：长弓发动 ONCE 后，普通返手仍保留 used=1，而原有恢复重建为 used=0，并可再次发动。原来的技能成员、数值和牌序一致不能证明资格一致，报告 passed 也不能承担完整玩家状态承诺。

v0.40.0 先实现后续计划的切片 A。新保存同时验证有序定义成员和独立默认 profile：实际 flags 必须与核实的构造默认值完全相等，不能包含 ONCE=0；used 必须为 0。即使新鲜未用 Bow 的 used=0，也因 flags=[0] 拒绝该资格范围。未知卡／升级／factory／tag／type／ordinal、不可读状态、动态添加或移除 flags、非零 used 均不能获得新版玩家资格证明。

这会收紧新保存的可接受牌组。既往可按有界数值范围保存的未知技能卡，不能因当前 flags=[]、used=0 自动成为默认来源正例。本切片没有拓宽所有卡牌支持；其改进是把保存时可证明的资格范围、恢复时核验结果和语义回退分清。

### 格式与依赖

主 Route C 仍为 schema 2，HAND 数值仍为 schema 3。新布局 Z6／T7／F10 添加必需的 `qualificationPolicy="flags-baseline-v1"`，该标记进入各自布局校验和，并要求独立 `route-c-exact-player-qualification.json`（Q1）。

| 格式 | 新资格版本 | 对应历史最高版本 | 说明 |
| --- | --- | --- | --- |
| player-zones | Z6 | Z5 | DECK/HAND 布局与所需 HAND3 依赖保留，新增 Q1 资格依赖 |
| player-trash | T7 | T6 | DECK/HAND/TRASH 布局与数值依赖保留，新增 Q1 资格依赖 |
| player-field | F10 | F9 | DECK/HAND/TRASH/FIELD 布局、场格和数值依赖保留，新增 Q1 资格依赖 |
| player-qualification | Q1 | 无 | `kind=route-c-exact-player-qualification`，仅证明本切片的 profile flags 与零 used |

保存时技能成员证明的固定最低版本仍为 Z4/T5/F8，D/T 基础生命固定最低版本仍为 Z5/T6/F9，不能随新的最高支持版本移动。历史 Z1..5、T1..6、F1..9 的校验和字节顺序保持不变，测试以全部历史版本的固定校验和向量回归。旧布局 struct 非空策略标记被拒绝；旧 JSON 夹带策略键，即使为空，也被拒绝；Q1 保留字段及动态卡／技能字段不能夹带进布局 JSON 以绕过历史校验和。

Q1 绑定主 payload 校验和、EXE SHA/大小、捕获时间、关卡/波次、所选布局 kind 与 payload 校验和、固定策略。它完整记录覆盖范围内的每张 D/H/T/F 卡，包括完整 CardInfoInstance、D/H/T 原生下标或 FIELD row/index，以及每个有序技能的 ordinal、factory、tag、type、profile id、规范 flags 与 used。跨恢复按有序位置和完整定义绑定，不把旧 GUID 或地址作为跨重启身份。

JSON 使用现有平面解析器的有界动态字段；显式保存 HAND/DECK/TRASH/FIELD 条数，即使牌区为空。总卡数上限 128，每卡技能上限 64；HAND 不超过 7，FIELD 按两个 row、每 row 五格；牌区下标与技能 ordinal 必须连续、FIELD 槽唯一且有序。flags 以唯一升序 0..7 的规范 CSV 编码，切片 A 再拒绝 0。缺字段、重复字段、额外字段、条数不符、位置重复、错完整 CardInfo 或错布局均拒绝。当前没有已审阅的零技能卡 profile，未知零技能卡也不能绕过门禁。

布局只哈希固定策略，Q1 再哈希所选布局 checksum，避免循环依赖。新布局缺失／损坏／错绑定 Q1 时不能降为“旧档未知”并继续精确层。校验和用于文件一致性与依赖绑定，不是对有意重写 schema 并重算 checksum 的真实性保证。

### 当前默认 profile 范围

默认 flags 来源由固定游戏 EXE 的 factory 注册、creator 和完整构造路径独立核实，不能从 CardInfo 的成员列表推断。实际完整 CardInfo 与选中升级成员仍由捕获、Q1 和恢复分别绑定。

| Card identity / ordinal | factory | effect tag | type | 默认 flags |
| --- | --- | --- | --- | --- |
| naturalApple@0 / 0 | player.esper.fruit.apple | naturalApple | 3 INIT | [] |
| naturalApple@1 / 0 | player.esper.fruit.apple.u1 | naturalApple.u1 | 3 INIT | [] |
| naturalLemon@0、@1 / 0 | player.esper.fruit.lemon | naturalLemon | 3 INIT | [] |
| naturalCherry@0 / 0 | player.esper.fruit.cherry | naturalCherry | 3 INIT | [] |
| naturalCherry@1 / 0 | player.esper.fruit.cherry.u1 | naturalCherry | 3 INIT | [] |
| spDeckEdit@0 / 0 | player.special.deckedit | spDeckEdit | 3 INIT | [6 BONDED] |
| spDeckEdit@0 / 1 | player.special.deckedit_manual | spDeckEdit_manual | 1 IGNITION | [6 BONDED] |
| genericUpdraft@0 / 0 | player.generic.set1.updraft | genericUpdraft | 3 INIT | [] |

这张表是八个 card identity、八种 effect factory、九个 card/ordinal 组合的范围；不表示八个实机恢复样本。`genericUpdraft@1` 含尚未独立审阅的成员，整卡不在本切片范围。`spDeckEdit@0` 的默认资格观测支持不授权重编程流程；重编程路线继续搁置。

### 捕获、发布与恢复门禁

捕获沿全量玩家 D/H/T/F 遍历取得 typed 只读结果，并复核 owner、成员、完整身份和资格。用于 Q1 的初始观测保留到写前，再次核对全量结果，不能用最后一次读数替换已经漂移的来源。不可读或不一致是 unavailable，不会转为空 flags 或 used0。

资格不支持时撤销依赖它的玩家布局、HAND 数值、回合与角色充能 proposal；主 Route C 与独立适用的未来 spawn 计划仍可按各自门禁完成保存。发布前将 Q1 主文件与备份纳入旧补充失效集合；所需 HAND 先于 Q1，Q1 先于所选布局发布。依赖发布失败不能发布该布局，也不能复用旧 Q1。

恢复先按 FIELD > TRASH > ZONES 选定布局，再预检所需 Q1 与主文件、EXE、时间及布局绑定。拒绝选定新布局的 Q1 后，不尝试另一份历史布局绕开拒绝，玩家／HAND／回合／充能层共同禁用，主 Route C 保持可用。

启动阶段检查新生卡完整定义、profile 默认 flags 与 used0，采用完整集合匹配，因为临时暂存位置尚不等于最终布局。全部布局与数值动作完成、MOVE/FIELD 抑制释放且真实队列安定后，再按保存的牌区、下标、场格、完整 CardInfo 和技能顺序做终验。三秒稳定窗口继续只读复核；漂移使用已有一次语义回退，不重复写资格或把临时技能数为零当通过。

### 报告与旧档含义

报告将保存端证明和恢复端结果分开：

| 场景 | 保存端证明 | 资格状态 | 可声明范围 |
| --- | --- | --- | --- |
| Q1 已预检绑定，随后终验和稳定窗口通过 | validated-Q1-layout-binding | verified-profile-flags-and-zero-used | 本策略覆盖卡的 profile flags 与零 used 相符 |
| 新布局必需的 Q1 缺失／损坏／错绑定 | required-proof-rejected | rejected-before-startup | 纯 Route C 预检降级，资格未通过 |
| 真正历史布局无 Q1 标记 | unknown-saved-state | legacy-bounded | 按历史有界布局／数值语义恢复，保存时资格未知 |
| 没有可接受的玩家布局 | not-applicable-no-player-layout | not-applicable-no-player-layout | 没有玩家资格恢复声明 |

具体字段为 `playerQualificationRequired`、`playerQualificationSavedProof`、`playerQualificationStatus`、`playerQualificationReason`、`playerQualificationPolicy`、Q1/布局 checksum。A 中 `playerOnceReplayCount=0`、`fullPlayerExactRestorationProven=false` 恒定。旧档不能从重建后的默认值补造保存端证明，旧证据也不升级为资格通过。requested/passed 与 preflight-route-c 的 passed 必须结合实际范围阅读。

### 实机结果与范围

正式 v0.40.0 OFF 构建通过 4/4 CTest，开发夹具关闭。DLL SHA256 为 `491D3AF72358FBD6F7C92725323D0815B201DB4CCD009441D65BAD417261119F`，实际编译 Mod 源码 SHA256 为 `215F69E4EB069C3187B9803F3FFE021A8DA0445E061B9D7E4ADBA586DBF4EDFA`；`build-01/build-validation.json` 绑定实际编译源码副本。游戏 EXE SHA256 为 `0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6`，大小 82718720。

| 样本目录 | 独立审计结论 | 范围 |
| --- | --- | --- |
| lemons-baseline-restored | verified-bounded-qualification，原 25 项全通过 | 真实 Z6/HAND3/Q1 捕获和恢复，8 张卡 |
| lemons-trash-restored | verified-bounded-qualification，原 25 项全通过 | 真实 T7/HAND3/Q1 捕获和恢复，8 张卡 |
| lemons-field-restored | verified-bounded-qualification，原 25 项全通过 | 真实 F10/HAND3/Q1 捕获和恢复，8 张卡 |
| lemons-trash-legacy-restored | verified-legacy，原 25 项全通过 | T6 历史布局，unknown-saved-state / legacy-bounded |
| lemons-field-legacy-restored | verified-legacy，原 25 项全通过 | F9 历史布局，unknown-saved-state / legacy-bounded |
| qualification-missing-restored | verified-rejected | 新布局缺 Q1，rejected-before-startup；preflight-route-c 保存语义可恢复 |
| qualification-wrong-binding-restored | verified-rejected | Q1 校验和有效但布局绑定错误，预检禁用玩家／HAND／回合／充能层 |
| qualification-bad-checksum-restored | verified-rejected | Q1 payload 校验和损坏，预检拒绝，无玩家战斗写入 |
| save-bow-unused-rejected | verified-refused-capture | 基础 Bow used0、flags=[0]，实际首失败 unsupported-once；旧依赖清空 |
| save-bow-clean-used-rejected | verified-refused-capture | 普通点击形成基础 Bow used1，实际首失败仍为 unsupported-once |
| save-bow-used-rejected | verified-refused-capture | Bow used1 可见，但实际更早失败为 HAND index1 mageLucia 的 unsupported-profile；不算已用 ONCE 首失败正例 |
| final-original | 原 Phase37 严格验证通过，原 25 项全通过 | 本轮备份的原用户 17 个文件连续两次恢复；资格仍为 legacy-bounded / unknown-saved-state |

Z6、F10 和两组 legacy 原始 comparison 的额外 `semanticEqual`、`enemyCardStateEqual`、`runtimeCardIdsEqual` 均为 false；T7 仅 `runtimeCardIdsEqual` 为 false。它们不属于原 25 项玩家验收集合，全部按原始文件保留；不能把玩家比较通过改写为敌人、GUID 或全战场相等。三个依赖拒绝的结果只证明正确预检降级，即使其原 25 项碰巧全通过，也不是 Q1 资格恢复正例。

Z6 主 checksum 为 `14855A8013DD73F5`、布局 `7F3CE31E702F5934`、HAND3 `0559828CC3D0A037`、Q1 `5190A1243479359F`。独立审计绑定真实源 F1、实际捕获 trace、恢复前输入副本及其预先记录的 SHA、报告与原始报告、恢复 trace、最终 F1 和 DLL/EXE。源 F1 可以位于保存前或保存后，但必须早于恢复输入且完整状态与 Q1 相同。秒级报告/F1 时间绑定对应毫秒 trace 的写出／导出开始，不能用导出结束迟于快照时间误判。初次时间口径错误的工具及失败仍保留于 `lemons-baseline-restored/audit-attempt-01/`；后续中间结果也独立保留。

T6/F9 输入是明确的离线准备：只调整真实来源的一张升级 Lemon 的位置及依赖，未生成 Q1。两组 legacy 恢复按旧有界范围验收；随后实际 F5 捕获产生 T7/F10 和真实 Q1，再恢复验收。基础 Bow 的干净牌组准备只离线将未审阅 Lucia 换为 Apple，不修改 flags/used；used1 由普通输入形成。离线输入不冒充自然资格捕获。

unused Bow 的旧补充清理验证检查了 `bow-unused-stale-seed.json` 中六个来源文件 SHA、准备时间与保存后缺失；没有独立的安装时字节快照，故不宣称安装动作另有实证。来源 collector 日志 PID 与捕获 trace PID 相同；旧 collector 未记录当时进程 EXE 路径／哈希，审计保留该历史进程属性未独立证明，当前固定 EXE 文件字节核验不代替它。

### 证据入口与续接

证据根为仓库旁的 `QuantumProtoclMod.runtime-evidence/20261015-player-qualification`，以下路径均相对于该根：

- `build-01/build-validation.json`、`build-01/source/`：生产构建、源码与 DEV 标记检查。
- `save-lemons-baseline/`、`save-lemons-trash/`、`save-lemons-field/`：真实保存端 metadata、trace 和 checkpoint；具体目录名以各恢复审计 manifest 的 captureMetadata 为准。
- 上表五个完整布局目录的 `qualification-audit.json`、`comparison.json`、`report.json`、`input-checkpoint-proof.json`、`trace.log`、`inventory.json`：完整正例及 legacy 原始结果。
- 三个 `qualification-*-restored/qualification-audit.json` 及真实输入副本：缺失／错绑定／损坏拒绝。
- 三个 `save-bow-*-rejected/refused-capture-audit.json` 与对应 source inventory、原 collector `.log`：实际首失败与依赖清空。
- `fixture-legacy-lemons-*-manifest.json`、`fixture-bow-profile-clean-manifest.json`：离线准备边界。

静态默认来源及工具保存在 `phase39-final-tooling/runtime/`，包含 `phase39-default-flags-profiles.md/.json`、研究脚本和反汇编原文。冻结归档共 57 个文件，另有 278 个固定外部文件，包括原始证据、实际构建副本及原 metadata 指向的 collector；原 metadata 未改写。`phase39-frozen-revalidation-20261014T193454988868Z/revalidation-index.json` 记录从冻结工具执行的 23 项审计、比较与自测，全部通过，261 个引用重新核验。完整原始 28 个比较布尔（25 个必需项加 3 个其他项）及差异原样复算。原 Phase38 ONCE 资格丢失与失败文件保留；A 通过拒绝收紧承诺，没有写回该缺口。

`final-original/phase37-restore-validation.json` 已通过；`session-cleanup.json` 记录游戏正常关闭、17 个检查点和 4 个 SaveGames 按本轮 fresh backup 逐字节还原，主 checksum `81095982711C71E2`，保留生产 DLL `491D…`。未操作 Genesis。阶段汇总和提交记录分别为 `validation-summary.json`、`commit-and-push.json`。这是 A 的里程碑收尾，后续独立推进 Phase40 B，不能把本次停机记录当作后续测试结束状态。

尚无自然动态 ONCE+used0 的本阶段实机覆盖；晚期捕获漂移、发布文件失败、启动规划拒绝及资格稳定窗口漂移未增加专门的生产故障样本。相应门禁、纯解析负例与旧阶段证据应分开阅读。B 仅推进基础 Bow 的受限一次回放，A 中回放次数仍为0，未扩大重编程、敌人或私有技能历史范围。
