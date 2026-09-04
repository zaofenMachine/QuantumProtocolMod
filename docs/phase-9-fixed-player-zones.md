# 第九阶段：固定顺序恢复初始牌库与手牌

日期：2026-09-04

版本：v0.11.3-dev

状态：普通地牢纯净小关开头的玩家牌库/手牌切片已闭环；主菜单直接恢复、在线验证、离线对照及恢复后导出均通过。

## 问题与选择

阶段 8 已恢复未来 `spawnList`，但同一 wave 的路线 C 重载仍会重新洗牌。公开 `ControllerCardGroup` 只有 `getCardInstanceListSorted()`，没有定向增删/移动接口；`Action_MoveCard_Resolve_Visuals` 又只是表现层，不能据此修改权威状态。

更低风险的入口是公开 `FDecklist.fixedOrder`：仍由游戏原生 `loadDeck` 创建卡牌、初始化状态并抽起手，只临时关闭洗牌并提供能产生保存牌区的固定顺序。

## 反汇编证据

证据绑定游戏 EXE SHA-256 `0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6`。

- `CardEngine:loadDeck` UFunction thunk RVA `0x101A170`，进入私有实现 RVA `0xE340C0`。
- 加载动作构造器 RVA `0xDFA7C0` 把 `FDecklist + 0x48` 的 `fixedOrder` 复制到动作对象 `+0xB0`。
- 加载动作执行 RVA `0xE41310` 在 `0xE422A5` 检查该字节。
- 值为 `False` 时执行 `0xE53430` 分支，创建并入队洗牌动作；值为 `True` 时直接跳过。

因此该方案不是私有容器写入，也不是事后移动卡面，而是使用游戏自身已有的固定顺序加载语义。

## 补充文件与固定顺序

新增 `route-c-exact-player-zones.json` schema 1，独立记录：

- Route C 负载校验和、EXE 指纹、关卡与 wave；
- `BP_ControllerDeck_C:getCardInstanceListSorted()` 的完整有序数组；
- `BP_ControllerHand_C:getCardInstanceListSorted()` 的完整有序数组；
- 自身负载校验和。

捕获只在牌库与手牌非空、合计不超过 128 张，且“标签 + 升级 + 数量”与 Route C 活动牌组完全一致时成立。任何场上、墓地、待处理区或其他缺失卡都会导致该可选补充被跳过，而不会影响主检查点。

恢复时把聚合 `DecklistCard.count` 展开为单张条目：先写保存后的牌库顺序，再反向追加预期手牌，并临时注入 `fixedOrder=True`。这使原生逐张抽牌得到保存时手牌。玩家牌区稳定后立即把 GameInstance 活动牌组恢复为 Route C 的普通值，避免固定顺序影响后续重编程或正常洗牌。

持久化测试覆盖 schema/校验和往返、损坏拒绝、卡牌总集合不匹配拒绝、升级等级、聚合计数展开、`dungeonTools` 抑制和固定顺序构造；CTest 为 `1/1` 通过。

## 失败样本与修正

### v0.11.0：牌区扫描崩溃

首次进入第一小关时生成 `crash_2026_09_04_13_48_15.dmp`。强制刷盘轨迹完成 Route C 全部 Getter，但停止在新增牌区准备之前，主检查点未被覆盖。

代码检查发现牌区查找器在判断对象是否 Deck/Hand Actor 之前对每个 UObject 调用了 `GetWorld()`。v0.11.1 改为先检查对象生命周期、完整类名、目标角色和活动实例，最后才对已确认的控制器读取 World。随后捕获完整经过控制器发现、Deck Getter、Hand Getter和三文件写盘。崩溃证据保存在仓库外 `20260904-v0110-player-zones-crash1`。

### v0.11.1：在线验证过早

首轮恢复报告为 `mismatch-semantic-fallback`，但数秒后的完整清单显示牌库、手牌及单卡状态全部一致。根因是第一次观察到 9 张卡齐全时，控制器排序仍处于动作结算的短暂状态。

v0.11.2 在首次全量但顺序不符后增加 2 秒稳定窗口。最终轨迹先记录 `awaiting-stable-order`，约 0.8 秒后逐字匹配并写 `verified`，随后才恢复普通活动牌组。

### v0.11.2：恢复后只读导出卡住

在线恢复已经 `passed / verified`，但随后 `Ctrl+F1` 卡在重载后的 `getCurrentDeckRun()`，没有写出新清单。这与旧 DeckRun 在多次重载后的已知不安全边界一致。

v0.11.3 在发生过 Route C travel 的进程中跳过 `getCurrentDeckRun` 和 `getActiveDecklistInstances` 两个冗余复杂 Getter，继续读取安全的 GameInstance 字段、所有牌区控制器、卡牌、敌人、槽位、资源、效果、计数器和 `spawnList`。F1 另有 `received → dispatch → begin → complete` 持久轨迹。活动牌组由路线 C 报告和玩家卡牌身份集合验证，不依赖这两个 Getter。

## 最终实机结果

最终构建：

- DLL SHA-256：`58621944B647CDB4495863FE8CF72AD13DBAF011ADE457056A0D3995AE098AF3`
- 部署 ID：`20260904-221011-230`
- 检查点：`cometDungeon1 / waveIndex=0 / 9/9 HP`
- 入口：完全重启后停在主菜单，直接按 `Ctrl+Shift+F6`
- 报告：`route-c-restore-20260904-141219-407.json`

报告结果：

- `status=passed`
- `exactSpawnPlanStatus=verified`
- `exactPlayerZonesStatus=verified`
- `activeDecklistRestoredAfterExactStartup=true`
- 特殊资源 `0/6 / True`

恢复后 `Ctrl+F1` 于恢复稳定窗口内成功写出 `battle-inventory-20260904-141217-884.json`，且没有卡住。与冻结基线归一化比较：

| 范围 | 结果 |
| --- | --- |
| 引擎、生命、特殊资源 | 一致 |
| 当前敌人、槽位、生命、回合、效果、计数器 | 一致 |
| 未来 29 个小关计划 | 一致 |
| 玩家牌库/手牌顺序 | 一致 |
| 玩家单卡状态与卡牌身份总集合 | 一致 |
| 运行时 GUID | 全部重建，0/12 共用；不计入语义相等 |
| `lastLevelChangeType` | `NORMAL → FAST`，当前唯一语义报告差异 |

用户确认主菜单恢复后的画面正常。最终证据保存在仓库外 `QuantumProtoclMod.runtime-evidence/20260904-v0113-main-menu-final`。

## 边界与下一步

本阶段闭合的是“所有玩家活动卡仍只在牌库/手牌”的纯净小关开头。只要已有卡在场上、墓地、待处理区或其他运行区，补充捕获就会安全跳过；项目仍不宣称中途战场的完整精确恢复。

下一步应先判断 `lastLevelChangeType=FAST` 是否只是已消费的 travel 诊断标记。若不影响玩法，可从精确玩法签名中降级；若影响后续流程，再用独立上下文补充恢复。之后再选择玩家场上/墓地或受伤敌人之一作为新的最小切片，不能把表现层事件当作权威状态。
