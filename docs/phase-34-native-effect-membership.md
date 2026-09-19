# 第三十四阶段：原生技能成员观测与删除技能漏洞

日期：2026-09-20。生产版本 v0.35.0。本阶段增加只读观测与清单比较，没有改变保存或恢复行为；下一阶段据此补拒绝边界，再继续动态基础生命。

## 原生观测

F1 新增 `nativeEffects:count/capacity/ordered/status/readError`。`ordered` 是保序 JSON 数组，每项包含内部 `tag`、数值 `type` 与原始 `factoryKey`。直接读原生卡 `+1B8` 的共享指针对数组，不调用会复制数组、增加引用的原生 Getter。effect `+38` 为 FName，`+90` 为类型，`+58` 的 FString 保留定义工厂键。

读取要求游戏线程、已验证 EXE、当前 world、有效 UObject 生命周期、引擎归属、共享引用与可读内存；效果数量限定 0–64、容量不超过 256，字符串检查容量、终止符及前后头部一致性。空原生数组是有效观测。未知工厂的空字符串保留原值，不能据此猜测定义。

UI overlay 数量/容量与 CardFace 身份、world、五项显示缓存独立记录；UI 失败不等于原生技能缺失，也不使成功的原生读取失效。任何失败明确记录 unavailable/readError。

`Compare-BattleInventories.ps1` 新增 `playerNativeEffectMembershipAvailable/Equal`。D/H/T 按原生 ID 数组绑定完整实例；FIELD 按唯一玩家槽位绑定；数组保持顺序和重复数。缺字段、错误状态或无效结构返回 unknown/null。STORAGE 与角色能力卡不属于本项覆盖。既有 UI 效果及动作历史比较保留，不能用成员相同掩盖历史不同。

## 正常操作复现的既有漏洞

可丢弃夹具只调整原生牌组组成和顺序，没有写入卡牌动态数值，也没有修改永久存档或解锁。初始 HAND：露西娅、格式化、上升涡流、两张苹果；DECK：樱桃、柠檬+、重编程。

1. 正常打出 `mageLucia`，原生技能为 `lucia_search` 与 `lucia_discard`，攻击/生命 2/2。
2. 正常打出格式化并选择露西娅：两技能被删除，原生数量由 2 变 0，攻击/生命仍为 2/2。
3. 上升涡流把露西娅返手，再正常执行后排将两张法术送入 TRASH。露西娅在 HAND 的原生技能仍为 0。
4. 现有捕获成功写入 HAND 和 TRASH 精确补充；恢复报告为 passed，但露西娅的两技能重新出现。

整段操作中露西娅完整 CardInfo 文本未变，恢复前为同一 GUID。因此完整定义相同和数值相同都不足以证明技能成员被保留。`natural-effect-removal-reproduction.json` 与前后完整清单、检查点、报告保存了证据。这个恢复是漏洞复现，不能记为精确技能恢复成功。

原始合成主文件把 `fixedOrder` 写在非规范字段顺序，第一次启动报告 `Restored active deck did not verify`。实际牌组成功建立后，通过正常 F5 导出游戏规范文本，再恢复该检查点通过；原失败保留，未改判。

FIELD 状态的露西娅被既有 plain-fruit 门禁拒绝；真正的漏检样本是她回到 HAND、FIELD 已清空之后。不要将 FIELD 捕获拒绝描述成同一漏洞通过。

## 回归与未解决事项

生产构建关闭开发夹具，4/4 CTest 通过。DLL SHA-256：

`1735B21D35D38036E5C75823A08930DC02FAA4FD654E46FBC70A88497E0EA493`

同进程连续恢复 one、mixed、combined、four、zero 五组已有检查点：每张 D/H/T/F 玩家卡的原生技能按序符合当前定义，原生数量与 UI overlay 数量一致。本轮没有复现第三十三阶段的 Cherry 显示缺口；旧失败仍不能凭本轮正常结果解释成 UI 问题或宣布修复。

比较器通过 PowerShell 7 和 Windows PowerShell 5.1 的合成边界测试，并核对旧真实清单的原有检查结果保持一致。实机删除技能样本作为新比较项的负例，正常重新创建作为正例。

证据：`F:/Project/QuantumProtoclMod.runtime-evidence/20260920-native-effect-membership`，最终清单见 `validation-summary.json` 和 `artifact-manifest.json`。

下一阶段：捕获前要求全部 D/H/T/F 玩家卡的原生工厂键按序符合完整定义，恢复时在原生移动及 FIELD 临时技能抑制之外重复验证；用新布局版本标记保存时已做该检查。旧版必须明确保存时成员未知。技能动作历史、动态私有状态、敌人精确恢复和重编程路线仍未接入。
