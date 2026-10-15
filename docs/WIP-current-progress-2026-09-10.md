# QuantumCheckpoint 当前工作进度（临时续接记录）

> 2026-10-15 本轮收尾，周额度最后进入收尾时剩约11%，用户阈值为约10%。Phase46只读状态工具已验收；Phase47时钟候选已构建但未部署。现场仍为v0.45.0，原数据完好、游戏关闭。下方历史结果不代表当前部署。

## 2026-10-15 第四十七阶段：自动保存时钟候选已构建，待实机

v0.46.0候选只扩已有自动capture的调度／合并／PROCESSING等待／重试／终态日志，记录原始steady ticks及period、当前ready、固定deadline；fresh scheduleReady为实际初始值，firstDueDerived明确为deadline-30秒推导。新增格式化在noexcept边界内，超界或失败回原日志一次；普通日志仍256字节，专用记录1536／栈2048。不改请求、期限、计数、身份或native控制流。独立审阅通过，生产OFF5/5CTest；尚无新游戏／日志故障注入／PROCESSING实测。详见[第四十七阶段](phase-47-capture-timing.md)。

候选DLL `D1AA93DBF5CF4E5C47946B6C2730E942A8D9738BC5AEE0CED3C56362B8F746CB`，Mod源码 `626C07AD195661540732FAD5DA45519EB580E1B1978A0B49D81A9563453C6F89`。证据根 `QuantumProtoclMod.runtime-evidence/20261015-capture-timing`，build-01保存候选及27源码／构建快照；验证SHA `42DC3C6CF212655840575AD7DC65757115E0B2917FD1E68DD8E1F5206DD53B3A`，summary固定35文件记录，SHA `433F6DEE908CB7FC7E09A9DEA5BBD690FB586C3FEF298F8845E8E3F5388FD179`。提交／推送记录另存该根目录。

本片23现场文件及CP／SaveGames集合未变，原17CP＋4SaveGames保持还原结果，部署仍v0.45.0 D526BAEF…E5A13，游戏关闭。下一轮先独立备份并部署核对过的v0.46候选，按[PROCESSING续测方案](next-automatic-capture-validation.md)最多做3次完整普通换波候选；不能把busy、手动拒绝、时钟推导或合成测试记成PROCESSING实测。不扩敌人精确恢复／重编程。

## 2026-10-15 第四十六阶段：只读保存状态已验收

代码和文档已提交推送 `cd373ac45569e42257a4cf735433f7644cc967e1`，当次远端HEAD核对一致。随后补充[日常使用指南](user-guide.md)，修正README操作区旧v0.40/Q1-only限制和历史机制被当成当前新捕获范围的歧义；按真实按键注册、自动保存调度、报告生成和资格表核对，未改变代码或扩大实测声明。

新增 `scripts/Get-CheckpointStatus.ps1` 及相邻模块，支持 `-GameRoot`／`-ModRoot`、中文输出与 `-Json`；PowerShell7.2+，实测7.6.5。将回执历史结果与当前磁盘关系分开，处理无回执legacy、陈旧／损坏／缺失文件、来源路径差异、类型／依赖矛盾及两次采样变化。只读固定路径，不判断恢复准入；所有结果保留两项恢复证明false。详见[第四十六阶段](phase-46-checkpoint-status.md)。

最终80项纯测试、9个阶段45材料离线案例和3个现场CLI入口通过；7个工具／设计文件、138个本轮证据冻结后再次复验80＋12，输出语义一致。manifest `943D8EC707B29811AAEC60C8925FEF9242485EFA05A0443757D0ACC51D5544A0`；复验入口 `phase46-frozen-revalidation-20261015T051414304050Z/revalidation-index.json`，SHA `05EB1402AA52864A752155C1D2DC42B5859757FC91A26FF18B04B54B45DD838B`。证据根 `QuantumProtoclMod.runtime-evidence/20261015-checkpoint-status`。

17CP＋4SaveGames＋DLL＋latest回执共23文件及原数据文件集合均未变；本阶段无游戏操作／DLL构建，保留v0.45.0 D526BAEF…E5A13。现场回执为上阶段manual4拒绝，而当前CP为已还原的原用户legacy，因此显示no-publication／当前来源未由该回执证明，符合事实。冻结工具和case禁止改写。下一轮先按[自动保存续测方案](next-automatic-capture-validation.md)补PROCESSING无提示等待：普通换波最多3次完整候选，未命中则先补最小只读见证；已有DEV busy夹具不能算PROCESSING正例。生命周期／监听失效和发布故障另开独立切片，不为制造覆盖放宽门禁，不扩敌人／重编程。

## 2026-10-15 第四十五阶段：保存结果回执已收尾

当前v0.45.0，生产OFF5/5CTest。新增 `Reports/route-c-capture-latest.json`，区分玩家布局／资格成套提交、主档单独提交、部分发布及主档前拒绝；七组件状态、实际首拒绝、attempt/time/binding独立记录，诊断异常不改产品控制流。恢复不读取receipt，未扩准入。详见[第四十五阶段](phase-45-capture-outcome.md)。

四份捕获审计实际为完整提交、main-only、预览未命中prompt的main-only、真正选择提示中的拒绝；原始意图与实际结果分开。新捕获恢复与原用户两次恢复比较均25/25，原28项及differences保留。16正70拒绝捕获组件自测，2正6拒绝清理隔离自测。新自动transient／实机写盘故障未覆盖。

证据根 `QuantumProtoclMod.runtime-evidence/20261015-capture-outcome`；冻结21工具／312本轮文件／297审计依赖，10项复验。manifest `5A7AF793101EB091EE72AFC0A0B90A440521E803880B40D61167E84990DAEE07`；入口 `phase45-frozen-revalidation-20261015T045049923478Z/revalidation-index.json`，SHA `A1B1BB055EAE2FC06AC22BF3B3725130F257CF610788B26E2A6E81C7C97536F3`。summary `F896753AA8A2BA19BD7B20F7E6B9CA6B88F85275719F5342698EEBEC07898717`，cleanup `81EC8A47CAAD042631BC40E8F05D0F70274BD24381C5F26248FA1766A4438EA5`。

原17CP＋4SaveGames逐字节恢复，main `81095982711C71E2`，游戏正常关闭，Genesis未碰；保留DLL `D526BAEF456ED40FAB4B591B8676FECF4020D431988FE807CE223EDD883E5A13`，Mod源码 `304943E4C8FB901A9D1D2E742DEB71D6A37F5A723D57532060B268912DE9E952`。构建01断言、首次路径错误审计请求、Int64清理前检拒绝均保留。全部冻结45工具和case禁止改写。

下一片Phase46：独立只读状态查看工具，明确“上次保存结果”与“当前磁盘档案是否匹配”，处理无receipt legacy、旧回执、损坏／缺失文件；不读取游戏内存或新增写入、不重启游戏、不扩大恢复承诺。当前周额度约剩18%，仍按10%左右收尾。新工具不得依赖runtime证据绝对路径，随仓库即可使用。


## 2026-10-15 第四十四阶段：双弓同排与HAND7覆盖已收尾

沿用v0.44.0，新增同前排／同后排及HAND7双弓实机覆盖，含满手牌新进程重复；四次F13恢复均25/25，原用户两次恢复的比较也25/25。12项冻结复验通过，原17个checkpoint和4个SaveGames逐字节还原、游戏正常关闭。产品代码与DLL未变。 详见[第四十四阶段](phase-44-bow-field-coverage.md)。

HAND7实际来源为13张：7苹果HAND、4升级樱桃TRASH、2基础弓FIELD；按正常出牌／执行构造，角色0/6 False，玩家HP5/13。两次恢复均12个手牌暂存动作，最后一张在双弓出牌、解除抑制及场地复核后完成。FIELD目标初始HAND／混合起点不可达，未扩域。13张首版生命上限不符的失败、首次审计误限全局唯一复核事件的失败均原样保留；最终4份审计重新核验。

冻结41个文件，固定4602个外部文件和338组目录集合。12项复验通过：4份F13覆盖审计、1份原用户legacy审计、5份原28项比较和2组合成自测；所有5份比较的required25均为true，额外3项及原始differences保留。覆盖自测4正／35拒绝，归档自测9正／68拒绝，清理纯逻辑7正／12拒绝。

冻结目录 `phase44-final-tooling`，manifest `89D2DC12AE35ED89DA9E6EDABD74F50DB069F6171AB1F1D6D79F1C5AAE2E88C3`；复验入口 `phase44-frozen-revalidation-20261015T040745752330Z/revalidation-index.json`，SHA `828E0D68C8E9B4AA147B0E9C02CA7786F1DFBAAD667CAAFD6F445A1870FD0878`。summary SHA `50F8904C10F995C084A9570FCDFAE6292D1140C4C41E2E1463F500476701D303`；cleanup SHA `A3801A51BE0EBD38FA66BDFDC433404628A343136F096788C3AB3C3B415BB9F4`。原17个checkpoint和4个SaveGames已逐字节还原，main `81095982711C71E2`，游戏正常关闭，原v0.44.0 DLL保留，Genesis未操作。提交与远端核验另记根目录 `commit-and-push.json`。

下一阶段采用 `runtime/phase45-capture-receipt-design.md`：为现有capture发布流程添加独立诊断回执，区分main、成套玩家布局/资格、独立turn/C3及部分写入，保留实际首拒绝原因。诊断不得改变已有保存/恢复、失败回退、准入、自动重试期限或原生写入。预算仍按用户更新的周剩余约10%收尾；新阶段先fresh backup、新证据根，旧冻结包不动。

## 2026-10-15 第四十三阶段：双基础长弓场上恢复已收尾

当前v0.44.0，生产OFF4/4CTest。F13仅恰两张基础长弓均在FIELD、默认数值、active=True；F12单长弓与F11普通果实含义不变。完整UI历史、inactive/COMPLETE、角色能力卡、敌人精确恢复和重编程不扩域。详见[第四十三阶段](phase-43-bow-field-pair.md)。

九次F13双长弓恢复覆盖00／10／01／11四种used向量、前后排槽位变化、苹果共存和新进程重复，均原25/25；其中四次00为初次恢复及普通操作／构造后续来源的重复基线，并非九种独立场景。两个F12单长弓used0/1、一个F11兼容及原用户两次回归也均25/25。三个Q2损坏输入实际在玩家重建前拒绝，原12/25保留；两个普通发动端点及一个已用点击控制分别验收。build01的vector终端索引失败和原Updraft HAND/TRASH重叠拒绝各11/25原样保留，后续修复／不同来源分别验收。

冻结75个文件，固定4182个外部文件与298组目录集合；43项复验通过（19份独立审计、22份原28项比较、2组合成自测）。冻结manifest `1116FA800C3CE4016C729DBB4207C3E49BA25979F7A56018EB93015F9DF4BA9D`，入口 `phase43-frozen-revalidation-20261015T031556872400Z/revalidation-index.json`。证据根 `QuantumProtoclMod.runtime-evidence/20261015-bow-field-pair`；正式审计使用audits-final及endpoint-final-*。

最终DLL `4C38912D0F495B34689F136941ADDF83CE6281E0BF551E64001C8C442DFE2DC6`，Mod源码 `69FE2DA2C9BCAE387A17448C2D724E2F0A7BE800097D9B355DC220A796F9CD01`。游戏正常关闭，17CP＋4SaveGames逐字节还原，main `81095982711C71E2`；最终DLL保留，Genesis未操作。summary SHA `500D83E1B0E0E9BFE1093FB7595832AB5638153ACB580EB15DBD9F6CD3F8848B`，cleanup SHA `6E00C6E7F136E0AAFF2E300013233EDE3196D6D63C537BCECB115F04D8AAC1CF`，提交及远端核验另记 `commit-and-push.json`。

本阶段已收尾，按用户更新的周额度剩余约10%阈值继续下一阶段：补双长弓同前排／同后排及至少9卡的HAND7暂存实测。只读检查确认完整恢复会先把初始手牌全部回牌库，因此F13进入场地准备时两Bow必在DECK；FIELD目标HAND／混合起点在现行流程不可达，不计作待补实测，也不为凑覆盖修改守卫。纯规划测试与实际可达场景分开记录。inactive/COMPLETE和特殊卡跨HAND/TRASH仍不扩域。

## 2026-10-15 第四十二阶段：基础长弓场上恢复已收尾

当前v0.43.0，生产OFF4/4CTest。F12仅一张baseBow位于FIELD，默认数值／active=True，Q2原义不变；F11继续普通果实。完整原生对象身份守卫跨越count0抑制与出牌，日志完整写入；证据失败与身份漂移分别锁存，终结清理不恢复成功声明。详见[第四十二阶段](phase-42-bow-field-placement.md)。

五个F12实际正例、一个F11兼容例和原用户两次回归均25/25；三个Q2负例均preflight拒绝，12/25原结果保留；两个普通操作端点独立计数。冻结65文件，固定2957外部文件／210目录集合，manifest `CC89A387F3FB9C5BEDEE577721678BB9AEEA0267B4ED01EE680018B41AA27EC6`。29项复验通过：12独立审计、15原28项比较、2合成自测。入口 `phase42-frozen-revalidation-20261015T014709316527Z/revalidation-index.json`，证据根 `QuantumProtoclMod.runtime-evidence/20261015-bow-field-placement`。

最终DLL `6924C976FF1CCE2E2C778059DBE6543DA4B37A5F1C3C50EB59B316E8426C7C6E`，Mod源码 `4897A1F79E88DE5FD97F35C72E0FD03AEEE2417EA95D78A6C256A4DDD68009CF`。独立cleanup已正常关闭游戏，原17CP＋4SaveGames逐字节恢复，main `81095982711C71E2`；新DLL保留，Genesis未操作。summary SHA `82F3C8DDB9CC6D0AF09EAC1F947D830CCD7109F3A9E7EBD2DAF9718B821C143B`，cleanup SHA `26EBAD9576F3D48E71CFBF6B042D49D13E885B378071164877419A318C113DE1`，提交推送另记 `commit-and-push.json`。所有冻结case／source／build目录禁止新增或改写。

下一片先读 `runtime/phase43-next-scope-review.md`：恰两张Bow均FIELD可复用已测placement与multi-target replay，但必须独立逐卡身份／槽位绑定、顺序抑制解除和0/1/2回放验收，F12旧含义不能放宽。直接发动后的inactive来源为False/COMPLETE，而本轮used来源为True/NONE；原25比较包含UI actionState，不能仅放开布尔值、改比较器或新增未审UI setter。跨HAND/TRASH/FIELD副本、新profile、敌人及重编程均后置。新阶段必须fresh backup、新证据根、新工具版本，旧A/B/C/42保持不变。

## 2026-10-15 第四十一阶段：角色资源许可守卫已收尾

当前 v0.42.0，生产 OFF 4/4 CTest，DLL `58867DE68C189005D287047FAAB2E30C25404F707FE7E9E8FAA06237355D863F`，实际 Mod 源码 `692079558207364BB16DC660B0DA2BACA642CD9B335F930F45715576BF57703B`。新角色资源 C3 保存实际规范 False／True、policy observed-allowance-match-v1，绑定主checksum／EXE／时间／关卡／波次；仅未满充，恢复前后与稳定采样必须匹配，不写许可bool。C1数值语义与unknown保留，C2 False-only保持独立。Q1/Q2玩家DHTF资格不包含角色能力牌生命周期。详见[第四十一阶段](phase-41-character-allowance-guard.md)。

七个资源正例均25/25（五个C3、两个C2／兼容）；两个相反许可启动案例均在玩家布局前失败并各一次语义回退。二十个资源审计加一个原用户legacy审计均完成各自范围；无资源／坏checksum／错时间及两个旧档启动差异保留24/25，不计精确正例。候选build03仅False捕获曾导致True2/6来源丢数值、恢复0/6，最终build04以C3修正，原失败保留。True／False与正常加载SaveGame／标题直接F6有本轮相关性，未证明完整因果或曾经满充假说。

证据根 `QuantumProtoclMod.runtime-evidence/20261015-character-allowance-guard`。原用户最终回归25/25，许可C1仍unknown。74个工具文件、2233个外部文件和152个目录集合冻结；44项复验通过（21验收＋22原比较＋1合成自测），10正例／57拒绝变体／4checksum向量均属合成审计工具自测。原28项和差异忠实保留。manifest `53F536F0FD85CEFB72DCB24BCB58A892EA9BA3300DC6E1A2FFDC13D75AAE05F4`；索引 `phase41-frozen-revalidation-20261014T222451560922Z/revalidation-index.json`。

`validation-summary.json` 和 `session-cleanup.json` 已完成；正常关闭游戏，fresh backup的17个checkpoint＋4个SaveGames逐字节还原，主checksum `81095982711C71E2`，新58867D DLL保留，未操作Genesis。summary SHA `EFDADFBFF9D76AF3852516DC04B0762E6DC8690F8DB66DD2B1AF4A8B58CB2B0D`，cleanup SHA `53A99257E141E5750F56A934ED511EF3D0785B2469633FE6AACD0131D82FA43E`。提交推送另记同根 `commit-and-push.json`；不能编辑r39/r40/r41冻结工具及固定case文件集合。

下一步 Phase42 先读 `runtime/phase42-bow-field-design-review.md` 及独立 persistence/native 审阅；只研究基础Bow在PLAYER FIELD，暂定独立F12准入、复用既有Q2，首片全D/H/T/F只允许一个baseBow。不要把Bow加进通用supports_plain_player_field_card，不改F11和旧Q2的效果语义；需先确认受抑制播放不会更换effect数组／descriptor，不得弱化header/owner/queue守卫。新阶段使用独立证据根和fresh backup。满充／能力牌、升级／动态ONCE、LEVEL、敌人精确及重编程仍不扩域。

## 2026-10-15 第四十阶段：基础长弓 ONCE 原生回放

当前 v0.41.0，生产 OFF 4/4 CTest，DLL `455F5575B3DC86A6B89E6A838511A144764496725368BF706F6344C7484AEE5D`，实际编译 Mod 源码 `78D2EB1EBFA7C38AAF5A2449C64E7719CFD094BB6E3765ED75EF9DF8182F1D96`。新 Z7/T8/F11＋Q2 支持基础长弓 used0/1，已用源只调用一次受守卫的 native true，未用源不写。资格策略 B 不等于重编程路线 B；重编程和敌人精确恢复仍搁置。

六组新恢复原25项均通过：单张未用、单张已用、同名双张混合、双张均已用、普通苹果 FIELD 共存、长弓 TRASH；回放次数0/1/1/2/1/1。三类必需资格证明缺失／坏 checksum／A policy 错配实机拒绝，纯 Route C passed。三个 fresh-process Q1 兼容案例与原用户 legacy 回归通过，原旧档资格仍unknown。御币自然刷新对照和恢复后普通操作端点记录完成；未宣称完整点击区间无动作、UI／私有历史、动态ONCE、升级长弓或长弓FIELD支持。详见[第四十阶段](phase-40-bow-once-replay.md)。

保留三次旧进程 A 兼容24/25失败，唯一必需差异为characterResourceEqual（abilityOk False→True）。当前与A构建中的充能恢复函数一致，不核验该资格位；同DLL正常重启后这三组25/25。该bool与0/6并存，不能简单认定满充因果。两次技能数组为空的自然源尝试也独立保留，不能当used0/1正例。

证据根 `QuantumProtoclMod.runtime-evidence/20261015-bow-once-replay`。原用户主checksum `81095982711C71E2`；独立fresh backup含17个checkpoint、4个SaveGames和旧A DLL。69个工具文件冻结、31项复验通过（含三个原24/25失败的忠实复算）；最终17个checkpoint和4个SaveGames逐字节还原，新455F DLL保留，游戏正常关闭。入口为 `validation-summary.json`、`phase40-frozen-revalidation-20261014T211550805192Z/revalidation-index.json`、`session-cleanup.json`；提交推送另记 `commit-and-push.json`。不得修改r39/r40冻结原始文件或继续往已固定case目录加文件。

后续先读 `runtime/phase41-ability-readiness-investigation.md` 的只读结论，再决定最小保守门禁；所有新证据使用独立Phase41目录和fresh backup，不复用本轮session。不要直接写许可bool、调用未审阅setter或清空能力卡。只读静态证据表明getter读取角色槽原生allow字节，不能把它当成“当前充能满”或“槽中有能力牌”的简单计算。

## 2026-10-15 第三十九阶段 A：资格默认来源门禁里程碑完成

当前 v0.40.0，OFF 4/4 CTest，DLL `491D3AF72358FBD6F7C92725323D0815B201DB4CCD009441D65BAD417261119F`，实际编译 Mod 源码 `215F69E4EB069C3187B9803F3FFE021A8DA0445E061B9D7E4ADBA586DBF4EDFA`。Z6/T7/F10＋Q1 的三个真实捕获／恢复样本与 T6/F9 的两个 legacy 样本均通过原25项玩家比较；其他 semantic/enemy/GUID 三项按原 comparison 逐项保留，不外推敌人或完整玩家私有历史。

三个必需 Q1 缺失／错绑定／坏 checksum 均实际 rejected-before-startup、preflight-route-c passed，无玩家布局／数值写入。基础 Bow unused 和 clean-used 两组实际 unsupported-once、旧依赖清空；prior used 样本实际先遇到 mageLucia unsupported-profile，独立保留。A 没有 used/flags writer，playerOnceReplayCount=0；Phase38 的 ONCE 恢复缺口仍是历史未修复结论。详情见[第三十九阶段 A](phase-39-player-qualification-baseline.md)。

证据根 `QuantumProtoclMod.runtime-evidence/20261015-player-qualification`。原用户检查点两次回归、全25项验证通过；17个checkpoint和4个SaveGames按本轮fresh backup逐字节还原，主checksum `81095982711C71E2`，游戏正常关闭，新DLL保留，未操作Genesis。57个冻结工具文件、278个固定外部文件及23项冻结复验通过，入口为 `validation-summary.json`、`phase39-frozen-revalidation-20261014T193454988868Z/revalidation-index.json`、`session-cleanup.json`，提交与远端核验另记 `commit-and-push.json`。这只是A里程碑，后续B测试结束还需独立收尾。

下一步继续 Phase40 B：只审阅基础 sakuraBow@0 的一次 native true 回放，保留明确 schema／policy 分支、完整源 proof、所有 owner/signature/queue/位置与全玩家前后守卫；未用源不写、同名副本按下标、失败不重试并仅一次语义回退。升级 Bow、Quarter 动态 ONCE、flags writer、重编程和敌人恢复仍不扩域；静态设计／签名证据不是支持声明。

## 2026-09-30 第三十八阶段：资格缺口已复现，本轮已收尾

当前 v0.39.0，生产 DLL `0F514C38A637D6A26D6497886A658FF86E632F8A6CC5A6D3263AA7C27A646881`，实际编译 Mod 源码 `3167A2FB4671020B0960FAAC16682E9C1C151D7D1C86DEE572204AC7B6F617D7`。本阶段只加入独立 F1 `nativeEffectQualification:` 观测，没有改保存、恢复、持久化或动作写入。生产 OFF 构建 4/4 CTest，开发夹具标记关闭，源码经独立范围审计。

实机已确认：长弓 ONCE 发动后通过普通上升涡流返手仍为 used=1；正常操作进入合法 T6/HAND3 保存范围后，恢复出的对应卡却为 used=0，flags 始终 [0]。重建 Bow 普通出牌并点击技能后再次 used0→1，UI NONE→COMPLETE/USED_ONCE。当前报告 requested/passed 不能代表完整资格恢复；未修改的 25 项比较中三项玩家历史相关检查仍失败，未放宽为通过。两个超范围保存和一次误拖苹果均保留，不计正例。

证据根 `F:/Project/QuantumProtoclMod.runtime-evidence/20260930-effect-qualification`。入口为 `validation-summary.json`、`freeze-revalidation-index.json` 和 `phase38-final-tooling`；跨恢复资格缺口与再次使用以修正后冻结工具复验为准，初版工具对前一结论文件信任过强的缺陷及原结果均保留。原用户检查点在最终观察版通过既有全 25 项检查；原 17 个检查点文件与 4 个 SaveGames 已逐字节还原，主校验和 `81095982711C71E2`。游戏正常关闭，临时最小化的 Genesis 窗口已还原，最终记录为 `session-cleanup-final.json`。首次纯预检失败的 `session-cleanup.json` 未改游戏/存档，不能当最终状态。完整范围见[第三十八阶段](phase-38-effect-qualification-observation.md)。第三十七阶段已提交推送 `eeafdff6cec00d219d051ba7729e792a31c42912`，54 个冻结工具文件及 16 组复验索引保存在其独立证据根。

下次优先处理资格覆盖声明和保守门禁，再单独推进受限 ONCE 恢复；具体设计随本阶段工具归档的 `runtime/phase38-next-qualification-plan.md`。先验证默认 flags 的明确 factory/profile，未知或动态变化不能按 used0 当作安全；旧检查点缺资格来源就保留 unknown，不能从重建值倒推。随后只在完整身份/签名/队列守卫下试验基础 Bow 的一次 used0→1，禁止调用 reset/false 或直接写 flags。补未用、同名两张、真正刷新、动态 ONCE+used0，以及恢复后普通尝试确实被阻止的对照。其余技能私有历史、LEVEL、独立生命调整、STORAGE/能力卡仍不扩域，重编程和敌人精确恢复继续搁置。

## 2026-09-30 第三十七阶段里程碑（历史记录）

v0.38.0 已实现 Z5/T6/F9 的 DECK/TRASH 非负基础生命增长与隐藏 CardFace 数值同步，HAND3 保持独立。最终生产 DLL `345C7D029506F7E1817FD04BC20D65914D535042B654B8E372DE8EC07DAD47BB`，Mod 源码 `7B932C51EE8EC912817978585113F7FC2E78F3EAA13BA545AC1C9784057F1179`；ON/OFF 各 4/4 CTest，生产测试标记关闭。八组最终构建恢复通过全 25 项玩家对照及适用 UI 检查，覆盖牌堆成长、同名副本、场地／墓地、生成卡、全墓地、旧 HAND2、重存 HAND3/F9 和原用户检查点；另有 AAD 五组与 CDEA 三组按各自 DLL 保留。

自动保存已增加两处 native idle 门禁、有界 busy 重试，以及只在读取前允许的 PROCESSING 无提示等待。固定请求／期限、world/engine weak identity 和单目标 spawner 删除见证避免跨生命周期重试；热重载关闭。DEV BD597 实际证明 busy 时 7 文件不变，正常 resume 后同请求保存、Apple 原生／UI计数变为 1。生产正常换波保存也通过。PROCESSING 等待、目标删除、监听关闭仍无实机通过声明；连续操作的第二组因 F1 迟到只保留有限结论。

早期隐藏卡缓存不一致、第一条双焰火 INIT 历史差异、CDEA 换波 non-OPEN 漏保存、缺 weak serial 的 2B4 拒绝、全局删除 epoch 过严的 10AB 拒绝，均保留原始证据，未放宽成通过。稳定窗口正常抽牌已验证只回退一次，不在验证后重复修补。完整范围见[第三十七阶段](phase-37-player-zone-base-health.md)。

阶段证据根为 `F:/Project/QuantumProtoclMod.runtime-evidence/20260930-player-state`，冻结工具在 `phase37-final-tooling`，最终索引／提交记录另存该根目录。原始备份23文件核对一致；原用户17个checkpoint已在阶段里程碑还原并恢复通过（主校验和 `81095982711C71E2`），游戏仍开着供本次授权测试。后续试验结束仍须再次核对还原checkpoint、4个SaveGames，关闭游戏并还原临时最小化的 Genesis 窗口，不能把本条当最终停机状态。

下一项是技能 ONCE 使用位与动态 flags 的只读观测和正常来源复现。Bow deck-only 夹具及只读 F1 候选已准备，尚不代表资格保存／恢复支持；新资料目录 `F:/Project/QuantumProtoclMod.runtime-evidence/20260930-effect-qualification`。重编程和敌人精确恢复继续搁置。按用户本次指示持续推进，周额度剩约10%时完成手头切片并收尾汇报。

## 2026-09-20 当前完成状态

v0.37.0 已实现 HAND3 动态基础生命、HEALTH 修正与普通计数联合恢复，修复恢复 MOVE 额外触发焰火成长，以及技能图标缺失/部分存活顺序不符。最终生产 DLL `C9D10C5FDD391D1E9652A4961E9B42E45C204FF2B2F8F375A0EA70E6F5497C95`，开发夹具关闭，4/4 CTest；14 组完整玩家观测比较、2 组历史可用范围及重存闭环、2 个版本拒绝和稳定窗口抽牌单次回退已验证。三次无效拖动不计负例；较早 DEV 的单张/八张 pending 清理独立记录，不外推八秒超时或 queue catch。

原用户检查点 17 文件已逐字节还原，主校验和 `81095982711C71E2`，最终恢复通过且游戏已关闭。阶段证据位于 `F:/Project/QuantumProtoclMod.runtime-evidence/20260920-player-hand-base-health`，提交/推送与完整范围见第三十六阶段及该目录 `validation-summary.json`。按用户最新指示完成本轮后停下，后续仍沿“小关重开＋玩家精确恢复”，不推进重编程或敌人恢复。技能私有历史、LEVEL/独立生命调整、满充能能力卡和 STORAGE 等缺口仍保留。

## 2026-09-20 上一阶段已完成状态

当前 v0.36.0 已加入玩家技能成员门禁：保存时检查所有 DECK/HAND/TRASH/FIELD 的原生技能工厂键与当前完整定义逐项一致，恢复时在布局前后及最终稳定窗口复核。新增 zones 4 / trash 5 / field 8 标记保存时证明；旧布局继续兼容，但报告明确为 `legacy-unknown`。HAND schema 2 不变。生产 DLL `DC2239856598C231E1FE41107E879723801A80E799F28708F574A26D40D3BABD`，开发夹具关闭，4/4 CTest 通过。

露西娅回手和苹果留场的真实操作均确认旧版漏洞：格式化删除技能后数值及完整 CardInfo 不变，旧版精确恢复会补回技能。新版已拒绝苹果 FIELD 及回手后的精确捕获，只保存普通 Route C 与有效未来刷怪计划；降级后的读取已通过。保存新主文件前先清理七类现行补充及其备份，首个文件被锁定时安全停止，主文件和原检查点文件均未改变。

旧/新 zones、trash、field，场地跨进程，生成苹果、生成卡退场空 HAND、全部牌在 TRASH 的回归均通过。14 组精确恢复回归已完成；稳定窗口操作干扰触发一次普通回退，原用户检查点另行恢复且 17 文件逐字节一致。比较脚本的 FIELD 枚举换序误报已修正并保留原始失败。完整结论以阶段文档和 evidence summary 为准。

证据目录：`F:/Project/QuantumProtoclMod.runtime-evidence/20260920-effect-membership-guards`。源码提交、推送与最终测试记录见该目录 validation-summary.json。

下一项验证 sorc 正常回手的动态基础生命，再按 HAND 范围实现 baseHP 与 HEALTH modifier 联合恢复。技能成员相同不代表动作历史恢复；第三十三阶段 Cherry 显示缺口原因仍未知。技能私有历史、更多生成卡、满充能、LEVEL 修正、独立生命调整、STORAGE/能力卡仍未补齐；重编程与敌人精确恢复继续搁置。


## 2026-09-10 历史调试记录

> 2026-09-12 续接更新：下文已归档为历史调试记录，请勿继续部署文中旧隔离版。当前正式实现、方向复核和验证见 [第十五阶段](phase-15-player-field-and-draw-authority.md)。抽牌根因已确定为原生基础倒计时与显示修正量不同；旧样本不能推导这两个值，需在新版重新采集。原始样本、失败报告和新样本均保存在 `QuantumProtoclMod.runtime-evidence/20260912-draw-authority`。最终构建的每轮验收以该目录 `validation-summary.json` 为准。

记录时间：2026-09-10（Asia/Shanghai）

> 这是一份工作中快照，用于在 Codex 限额、重启或上下文丢失后继续调试。当前内容尚未提交；运行时结论通过最终复测后，再整理进正式阶段文档。

## 当前方向

用户已经确认先闭环 Route C，再逐层探索精确恢复。Route C 的普通 `DUNGEON` 小关语义重开已可用；当前工作已经进入“在稳定 Route C 上叠加可独立失效的精确补充层”阶段。

方向没有改成内存快照或整进程序列化。当前方案仍是：

1. 用 Route C 负责稳定地回到同一普通小关和同一原生波次语义。
2. 每类精确状态使用独立、与 Route C 校验和绑定的补充文件。
3. 每个写入都先验证对象身份、函数签名或原生布局，写后立即验证并跨帧验证。
4. 某个精确层失败时安全降级，不破坏 Route C 的基础恢复。
5. 通过真实运行时样本逐层闭环，不一次性恢复所有未知状态。

这个方向目前仍合理。现阶段主要风险不是总体架构，而是 Unreal/Blueprint 内部派生状态：某些数值能写对，但相关的可交互状态会在下一帧被游戏重新计算。

## Git 与工作区

- 分支：`main`
- 最近一次已提交并推送：`d2573e5 Validate player field movement primitives`
- 之前相关提交：
  - `967b811 Restore exact turn progress`
  - `dd750ae Restore exact player trash`
  - `d1a1075 Restore exact partial character charge`
  - `50d8466 Validate complex combat gaps and downward health`
  - `3fd782b Restore exact initial player zones`
  - `6226d8f Restore exact future spawn plans`
  - `300d736 Close Route C restore vertical slice`
- 当前有未提交改动，不能丢弃：
  - `cpp/QuantumCheckpoint/include/CheckpointPersistence.hpp`
  - `cpp/QuantumCheckpoint/include/CheckpointSchema.hpp`
  - `cpp/QuantumCheckpoint/src/CheckpointPersistence.cpp`
  - `cpp/QuantumCheckpoint/src/QuantumCheckpointMod.cpp`
  - `cpp/QuantumCheckpoint/tests/CheckpointPersistenceTests.cpp`
- 当前未提交差异规模约为 2087 行新增、83 行删除。主体是完整玩家场地恢复、攻击状态恢复及抽牌可用状态实验。
- 在最终运行时通过前，不提交这批改动。通过后按用户约定：提交后顺手直接推送。

## 已经闭环或验证通过的能力

### Route C 基础恢复

- 普通 `DUNGEON` 小关可保存并从主菜单语义重开。
- 自动保存只在受支持、稳定的普通小关触发。
- 教程、Boss 或缺少完整战斗对象时拒绝保存/恢复。
- 主检查点损坏、过期、EXE 指纹不匹配或补充文件不匹配时安全拒绝或降级。

### 已验证的精确补充层

- 未来敌人生成计划：最终样本中 `spawnList` 全量一致。
- 初始纯牌库/手牌状态：固定顺序启动后精确恢复，并及时撤销固定顺序注入。
- 新掉落物区域：三张掉落卡在恢复后可于重编程界面正确显示。
- 墓地：最终样本中苹果、苹果、樱桃的顺序正确恢复。
- 角色生命：可以向下恢复；UI 先显示启动值、稍后稳定到目标值属于预期恢复阶段。
- 未满角色特殊资源：`4/6` 样本已正确恢复；满充能仍因可能生成能力卡而保守排除。
- 回合数、玩家抽牌倒计时数值、威胁进度数值：原生字段写入和 UI 显示曾分别验证。
- 玩家场地：
  - 使用固定顺序把目标卡暂存到手牌。
  - 临时抑制入场效果，再通过原生出牌路径放到准确格位。
  - 恢复每张卡的当前生命值。
  - 恢复每张卡本回合是否还能攻击。
  - 运行时报告已达到位置、生命、攻击状态 `mismatches=0`。
  - 入场效果没有重复触发，这是恢复“已经在场”的卡时的正确语义。

## 当前保存样本

检查点目录：

`F:\SteamLibrary\steamapps\common\Quantum Protocol\Quantum\Binaries\Win64\Mods\QuantumCheckpoint\Checkpoint`

主样本属于：

- 关卡：`cometDungeon1`
- 小关/波次：第二小关，`waveIndex=2`
- 玩家生命：目标值 7
- 角色特殊资源：`0/6`
- 卡牌引擎回合：14
- 玩家抽牌倒计时：0
- 威胁/波次警告原生计数：0
- 牌库：2 张（电池、重编程）
- 手牌：4 张（樱桃、樱桃、柠檬+、春）
- 墓地：空
- 场地：2 张苹果
  - `(1,5)`：当前生命 2，保存前已攻击，恢复后应不可攻击。
  - `(2,2)`：当前生命 1，保存前受伤，仍可攻击。
- 精确场地状态串：`0,4,2,0;1,1,1,1`

当前 `route-c-exact-turn-progress.json` 是旧格式样本，没有新加入的 `playerCanClickToDraw` 字段。代码对旧文件保持兼容，并根据“倒计时为 0、牌库非空、手牌少于容量”推导目标应为可抽牌。

## 玩家场地恢复的关键实现证据

- 原生 `isTurnActive()` Getter 的游戏 RVA 为 `0xE27CA0`。
- 已确认机器码为直接读取卡牌原生状态 `+0x19C`：
  - `0F B6 81 9C 01 00 00 C3`
- 反射 thunk RVA 为 `0x102D6A0`。
- 写攻击状态前会验证：
  - 游戏 EXE 签名；
  - 反射函数指针；
  - 目标地址可写；
  - 公共 Getter 与原生字节一致。
- 写后再次验证 Getter 和原生字节，因此 `(1,5)` 苹果恢复为不可攻击已经得到运行时确认。

## 当前唯一未闭环的问题：倒计时为 0 时牌库不能持续可抽

### 已观察现象

最初只恢复数值时：

- 抽牌倒计时 UI 显示 0。
- 牌库没有亮起。
- 点击牌库不能抽牌；连续按两次 `Ctrl+F1` 也没有抽卡，随后正常游戏逻辑使计时变为 4。

随后加入：

- 保存/恢复 `ControllerDeck.currentCanClickToDraw`；
- 当目标从 False 恢复为 True 时，调用 `OnCanClickToDrawJustActivated`。

测试结果：

- UI 顺序是 `0 -> 牌库亮一下 -> 5`。
- 其他恢复内容全部正常。
- 这不是纯显示问题。日志表明写入当帧为 `drawDelay=0`、`currentCanClickToDraw=True`，约 0.8 秒后游戏把它变回 `drawDelay=5`、`currentCanClickToDraw=False`。
- 模组检测到跨帧不一致后安全回滚了回合进度写入。

相关旧版报告：

`F:\SteamLibrary\steamapps\common\Quantum Protocol\Quantum\Binaries\Win64\Mods\QuantumCheckpoint\Reports\route-c-restore-20260909-173711-413.json`

报告中的关键结果：

- 玩家场地：`verified-position-health`，攻击状态不匹配数为 0。
- 回合进度：`failed-rolled-back`。
- 目标：回合 14、抽牌倒计时 0、可抽牌 True。
- 回滚后观察：回合 0、抽牌倒计时 5、可抽牌 False。

### 当前隔离假设

`currentCanClickToDraw` 很可能是 Blueprint 的边沿检测缓存，而不是唯一的权威可交互开关。可能存在类似逻辑：

1. 每帧计算真正的 `canTurnDraw()`。
2. 把计算结果与 `currentCanClickToDraw` 的上一帧值比较。
3. False -> True 时触发 `OnCanClickToDrawJustActivated`。
4. 某条激活或消费路径又把抽牌倒计时设回 5。

尚不能确定是显式调用 `OnCanClickToDrawJustActivated` 导致重置，还是游戏逐帧逻辑看到 `currentCanClickToDraw=True` 后自行消费状态。

## 当前已部署但尚未运行验证的隔离版

- 当前部署 DLL SHA-256：
  - `787997B25BBD5DB80B402B39A93486F27A8874BBFB13184CA55F88C3872CB775`
- 编译成功。
- 持久化测试：1/1 通过。
- 部署前已验证并关闭以下测试进程：
  - `F:\SteamLibrary\steamapps\common\Quantum Protocol\Quantum\Binaries\Win64\Quantum-Win64-Shipping.exe`
  - `F:\SteamLibrary\steamapps\common\Quantum Protocol\Quantum.exe`
- 当前隔离版仍恢复：
  - 回合数；
  - 抽牌倒计时；
  - 威胁计数；
  - `currentCanClickToDraw`。
- 与上一版的唯一关键差异：完全不调用 `OnCanClickToDrawJustActivated`，只写可抽牌缓存并跨帧观察。
- 这版尚未启动游戏测试，不能提交。

## 下一步唯一需要执行的测试

热键必须注意：

- `Ctrl+Shift+F5`：保存 Route C 检查点。
- `Ctrl+Shift+F6`：恢复 Route C 检查点。
- `Ctrl+F1`：导出只读运行时清单。

测试步骤：

1. 启动游戏。
2. 等主菜单稳定。
3. 在主菜单按 `Ctrl+Shift+F6` 恢复现有检查点。
4. 进入第二小关并稳定后，先不要点击牌库，等待约 3 秒。
5. 记录以下三种结果之一：
   - 倒计时保持 0，牌库持续亮起；
   - 倒计时保持 0，但牌库不亮；
   - 牌库短暂亮起或不亮，倒计时再次自动变成 5。
6. 同时快速确认场地位置、生命和 `(1,5)` 苹果不可攻击仍正常。

结果解释：

- 若保持 `0 + True`，说明上一轮是显式激活事件导致重置；下一步再单独解决 UI 激活，不动权威计时。
- 若仍变成 `5 + False`，说明只写缓存也会被游戏逐帧逻辑消费；下一步需要找到 `canTurnDraw()` 的真实依赖或控制其计算输入，不能继续直接伪造缓存。
- 若保持 0 但不亮，需测试牌库是否实际上可点击，以区分“仅缺 UI 事件”和“仍不可交互”。

测试后优先读取：

- `route-c-trace.log`
- 最新 `Reports/route-c-restore-*.json`
- 必要时再按 `Ctrl+F1` 导出 inventory；不要要求用户复现完全相同的 roguelite 场面。

## 后续工作顺序

1. 闭环抽牌倒计时为 0 时的真实可交互状态。
2. 用最终运行时证据整理 `docs/phase-13-turn-progress.md`，纠正“数值 Getter/UI 相等即闭环”的旧结论。
3. 把完整玩家场地恢复整理为新的正式阶段文档，并更新 `docs/technical-status.md`。
4. 把最终报告、清单、检查点和关键 trace 复制到仓库外的运行时证据目录：
   - `F:\Project\QuantumProtoclMod.runtime-evidence`
5. 运行构建、单元测试和 `git diff --check`。
6. 提交全部正式改动并立即推送到 GitHub。
7. 再继续精确敌方场面、敌人当前生命/行动倒计时以及其他尚未建模的战斗中间态。

## 操作与安全约定

- 用户已明确允许开发期间需要时直接杀掉游戏进程；测试关卡可丢弃，正常存档已备份。
- 杀进程前仍要校验进程完整路径，只停止上文列出的两个 Quantum 可执行文件。
- 游戏运行时不要覆盖 DLL；先关闭游戏，再卸载和部署模组。
- 构建：`.\scripts\Build-CppMod.ps1`
- 部署：先 `.\scripts\Uninstall-CppMod.ps1`，再 `.\scripts\Install-CppMod.ps1`
- HTTPS 推送失败时使用：
  - `git -c url.git@github.com:.insteadOf=https://github.com/ push origin main`
- 测试要求必须放宽，不能要求 roguelite 每轮出现完全一致的卡牌和场面；只约束本轮真正需要验证的状态。
