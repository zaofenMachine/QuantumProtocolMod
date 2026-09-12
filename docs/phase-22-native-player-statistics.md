# 第二十二阶段：原生攻击力与属性修正观测

日期：2026-09-13（Asia/Shanghai）。版本：v0.23.0。

## 状态来源

此前的独立库存比较没有读取当前攻击力，`cardModifiers` 也未取得有效内容，因此同名卡牌的攻击增益可能被旧比较遗漏。本阶段扩展只读导出和比较，不增加动态属性恢复写入。

已验证 EXE 中，卡牌原生状态的 `+0x124` 是基础攻击力，`+0x130` 保存属性修正表。`E1CA60` 遍历其中 `ATTACK` 修正，累加 `Amount`，加上基础值并钳制至零以上。卡面刷新路径 `FB9282` 调用这个计算函数和基础攻击 getter `E1DF30`，随后更新 CardFaceWidget 的当前值与基础值。

导出先验证 EXE 完整指纹、相关机器码、活动世界和 CardEngine 所属关系，再只读遍历稀疏存储及其分配位图。遍历检查槽位、容量、空闲项数、可读范围和成员数；每条修正保存键、Tag、stat、Amount、limit 以及排序后的 flags。解析出的攻击力还必须等于原生 getter。没有复制进程指针进检查点，也没有修改容器、引用计数或卡面数值。

库存新增 `nativeStats:*`。`nativeStats:status=verified-native-attack` 表示本轮攻击力与修正解析通过；失败时记录 `nativeStats:readError`。卡面数值作为交叉观测单独输出，不作为原生权威来源。

## 实机与比较验证

普通八张牌样本的 DECK、HAND、FIELD、TRASH，以及库存中可见的 STORAGE 卡均成功读取。春从墓地苹果生成的新场地苹果，基础攻击 2、当前攻击 3，原生表中存在 `springBuff`、ATTACK、Amount=1、limit=-1、空 flags；卡面显示同样为 3。执行该行后，同一运行时卡进入 TRASH，修正表清空，攻击回到 2。对应库存为 `231235-451` 和 `231413-483`，保留运行时 ID 用于此次同进程跟踪。

七张手牌加场地和普通开局各完成一份新版保存前后比较，原有玩家项及新增攻击力／修正项均一致。此处是无修正样本的恢复回归；带增益的生成卡样本只证明观测，仍超出完整活动牌组约束，不宣称已经精确恢复。

比较脚本新增 `playerNativeStatisticsAvailable` 和 `playerNativeStatisticsEqual`。只有两份库存全部玩家卡都含有效原生统计时才比较；缺失时相等性为 null。原 `playerCardStateIncludingLocationEqual` 保持旧字段口径，说明中明确其不覆盖新增统计。

独立比较夹具验证三种情况：实机库存自比较通过；仅在报告副本中改变增益苹果的 currentAttack，旧检查仍相等而新增检查正确报告差异；移除统计字段后结果为未知。这些 JSON 副本仅用于比较工具测试，没有写入游戏或检查点。

构建与 CTest 通过。正式 DLL SHA-256：`0B7EFEFDBA784239E9E81D013C8111A2202EF605CB904C575B177805A26DFC1D`；部署 ID：`20260913-070915-438`。完整报告、源代码和测试材料位于 `QuantumProtoclMod.runtime-evidence/20260913-native-player-statistics`。

## 剩余工作

继续验证原生属性修正的添加、清除与回滚，再接入保存和恢复。当前真实增益样本只有单条 ATTACK 修正、空 flags；ONE_ATTACK、DECAY、多条修正、生命或等级修正以及计数器尚未完成对应实机覆盖。HUD 悬停缓存仍不能替代这些原生状态。
