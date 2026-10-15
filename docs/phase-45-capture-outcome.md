# 第四十五阶段：保存结果回执

2026-10-15，v0.45.0。沿用“小关重开＋受限玩家恢复”，新增持久诊断文件 `Mods/QuantumCheckpoint/Reports/route-c-capture-latest.json`。恢复流程不读取它；玩家准入、原生操作、补充档格式、自动等待期限和失败回退均保持原义。

## 实际用途

过去的 `capture.complete` 只表示主档写完，后续玩家补充档仍可能失败。现在每次终态保存单独记录：

| outcome | 含义 |
| --- | --- |
| `player-bundle-committed` | 选中的玩家布局、资格及所需 HAND 依赖已提交并绑定；不代表完整玩家快照 |
| `main-only` | 主档已提交，未形成支持范围内的玩家布局／资格组合 |
| `partial-publication` | 选中组件未全部提交、依赖不匹配或诊断不完整 |
| `rejected-before-main` | 本次未提交新主档；不能据此推断所有旧文件都未变 |

七个组件分别记录主档、生成计划、HAND、资格、布局、角色资源和回合进度的选择／写入状态、payload checksum、写后观察 SHA，以及首次失败和首次玩家拒绝。`allSelectedComponentsCommitted` 只覆盖此次实际选中的组件。回执始终声明 `fullPlayerExactRestorationProven=false`；可选组件失败不能被较窄的布局成功掩盖。

诊断自身失败不会抛入保存或重试逻辑。文件写入成功后立即标记 committed，再做 SHA 和日志观察；后续日志失败不改称文件未写入。latest 回执也可能陈旧，必须结合 attemptId、时间和实际文件判断；它既不是恢复成功报告，也不是运行时准入检查。

## 实机结果

本轮使用新生产 DLL，进程 72816，关闭开发夹具，5/5 CTest 通过。

| 实际操作 | 回执 | 验证 |
| --- | --- | --- |
| 双弓同前排、HAND3 的支持场景正常 F5 | 七组件全部提交，`player-bundle-committed` | 独立文件 SHA／FNV、Q2／HAND／F13、F1 与同次日志核验；使用新存档恢复后原25项全部通过 |
| 正常发动前排左弓，使其不再满足当前 FIELD 状态范围 | `main-only` | 保留实际首次布局拒绝原文；不将主档成功称为整次保存失败 |
| 点击手牌上升涡流图标，仅显示目标预览 | `main-only` | 首次场景构造未命中真正提示，原目录 `prompt-refused` 保留；目录名不作为结果 |
| 真正拖出上升涡流，原生目标选择仍打开时 F5 | `rejected-before-main` | 原因是 `The battle is not in the stable OPEN state`；前后整个 checkpoint 文件集合和字节相等，同次日志无发布 |

最后一例的截图与实际拒绝日志证明该端点场景；没有声称逐帧连续观察原生提示，也不把端点相等说成整个区间绝无写入。自动 transient 重试与实机磁盘写入故障没有新增实测；部分发布、孤立 Q/HAND、哈希不可用和诊断故障用纯组件测试覆盖。

原用户检查点另在新 DLL 下恢复两次，原25项比较通过；legacy 保存时资格仍未知。所有比较保留原28个布尔值和差异，敌人、实例 ID 等额外3项没有删改或改成成功。

## 修正与归档

首次构建因 MSVC 的聚合构造 noexcept trait 断言失败。保留总断言，增加显式默认构造和逐成员无异常保证，并拒绝会分配调试代理的 ABI；第二次生产构建5/5通过。原用户审计首次请求因路径分隔符转换仍引用旧会话 F1 而被拒绝，另建 v2 请求绑定真实来源。清理首次在写入前因 JSON Int64 被 Int32-only 检查拒绝，修成严格接受 Int32/Int64、拒绝 bool/double/string。原失败输入及输出保留。

证据根 `QuantumProtoclMod.runtime-evidence/20261015-capture-outcome`。冻结21个工具文件、312个本轮证据文件，另固定297个审计依赖记录；10项冻结复验通过：4个捕获审计、1个新存档恢复审计、1个原用户 legacy 审计、2个原28项比较及2组纯测试。捕获审计组件测试16正／70拒绝，清理隔离测试2正／6拒绝。

冻结 manifest `5A7AF793101EB091EE72AFC0A0B90A440521E803880B40D61167E84990DAEE07`。复验入口 `phase45-frozen-revalidation-20261015T045049923478Z/revalidation-index.json`，SHA `A1B1BB055EAE2FC06AC22BF3B3725130F257CF610788B26E2A6E81C7C97536F3`。summary SHA `F896753AA8A2BA19BD7B20F7E6B9CA6B88F85275719F5342698EEBEC07898717`，cleanup SHA `81EC8A47CAAD042631BC40E8F05D0F70274BD24381C5F26248FA1766A4438EA5`。

游戏正常关闭，原17个 checkpoint 与4个 SaveGames 已逐字节还原，主checksum `81095982711C71E2`；Genesis 未操作。保留 v0.45.0 DLL `D526BAEF456ED40FAB4B591B8676FECF4020D431988FE807CE223EDD883E5A13`，Mod源码 `304943E4C8FB901A9D1D2E742DEB71D6A37F5A723D57532060B268912DE9E952`。

下一步提供独立只读状态查看工具，帮助用户识别上次保存结果与当前磁盘档案是否对应。完整玩家恢复、inactive/COMPLETE、角色能力牌、敌人精确恢复与重编程继续不扩域。
