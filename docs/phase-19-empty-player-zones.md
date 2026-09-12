# 第十九阶段：空牌库与空手牌

日期：2026-09-13（Asia/Shanghai）。版本：v0.20.0。

## 实现范围

schema 2 玩家补充不再把空 DECK、空 HAND 一律当作损坏数据。场地补充仍要求 FIELD 非空，独立墓地补充仍要求 TRASH 非空；完整活动卡集合必须一致且在 1–128 张之间。旧 schema 1 的非空限制保持不变。

这一步复用现有原生创建、固定牌序和逐卡分配流程。空目标区域意味着不保留该区域的副本，启动时暂存在手牌或牌库的对象仍按保存顺序迁往墓地和场地。没有直接编辑游戏的牌区容器或改变原生开局抽牌数量。

## 实机样本与来源

空牌库样本由普通出牌、效果抽牌和行执行生成，没有编辑检查点卡牌负载。一个样本有三张手牌、四张墓地牌和一张场地苹果，抽牌基础倒计时为 2；另一个有两张手牌、六张墓地牌、空场地，倒计时为 0。

在选择提示进行中，`currentCanClickToDraw` 会临时为 False；提示结束回到 OPEN 后，空牌库且基础倒计时 0 的值为 True。本阶段分别验证了 2/False 和 0/True，继续由游戏派生交互缓存。

空手牌使用显式开发夹具生成，不能称为普通游戏操作样本。夹具仅把当前手牌按原生顺序通过 DEFAULT MoveCard 移至墓地，不出牌、不触发重编程、不修改检查点。两个样本分别为空 HAND、非空 DECK/FIELD/TRASH，以及全部八张活动牌位于 TRASH。保存后彻底退出，切换到不含夹具的正式 DLL，再读取这些检查点。

开发开关 `QUANTUM_CHECKPOINT_RUNTIME_TEST_FIXTURES` 默认 OFF；构建脚本每次显式设置它，避免沿用旧 CMake 缓存。需要造样本时使用 `Build-CppMod.ps1 -EnableRuntimeTestFixtures` 和 `Install-CppMod.ps1 -AllowRuntimeTestFixtures`，开发 DLL 的 `Ctrl+Shift+F10` 才会生效。安装器默认拒绝带夹具的 DLL，部署清单记录开关状态。夹具报告只证明动作已入队，完成结果以随后独立的只读库存报告为准。

## 验证

构建和 CTest 通过。离线测试覆盖空手牌场地分配、空牌库场地分配、全部卡牌在墓地、旧格式拒绝空区域、升级身份不匹配及完全空活动牌组拒绝。

正式 DLL 下的空手牌与场地、全部卡牌在墓地、空牌库计时 2、空牌库计时 0 均已通过。独立比较同时验证玩家完整实例、原生区域顺序、单卡状态与位置、生命、充能、回合和未来生成计划。所有这些玩家项一致；敌人当前运行状态允许不同。完整报告和额外新进程/旧格式回归以证据目录中的 `validation-summary.json` 为准。

证据目录：`QuantumProtoclMod.runtime-evidence/20260913-empty-player-zones`。普通样本为 `checkpoint-empty-deck`、`checkpoint-empty-deck-zero`；夹具样本为 `checkpoint-empty-hand-field-fixture`、`checkpoint-all-trash-fixture`。`checkpoint-draw-effects-intermediate` 不是空手牌，保留它仅用于说明效果抽牌和未支持身份组合的失败回退。

正式 DLL SHA-256：`926EFD3BB7A17D9BAA6352CEE65695156200DE4A5CF4ED3E251C064C2B957102`；部署 ID：`20260913-054819-415`。

## 下一步

仍需扩展原生开局抽五张与目标手牌数量不一致时的暂存规划，包括较大手牌、需要把启动手牌返还牌库的布局。纯 DECK/HAND 层仍靠原生启动直接匹配，不能据本阶段声称任意空手牌布局都能恢复。PENDING、特殊卡跨手/场/墓同身份、额外生成卡、满充能及更多卡牌动态状态仍受限制。继续沿小关重开和玩家补充推进，重编程重开与敌人精确恢复保持搁置。
