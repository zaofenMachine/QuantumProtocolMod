# 第十三阶段：回合进度精确恢复

> 2026-09-12 修正：本阶段的 schema 1 和“只写修正量”只能证明显示值恢复，不能证明抽牌交互恢复。原生 readiness 只检查基础倒计时；v0.16.0 已改用 schema 2 恢复两个原始字段并验证真实抽牌。本文保留历史证据，当前实现以 [第十五阶段](phase-15-player-field-and-draw-authority.md) 为准。

日期：2026-09-08

版本：v0.15.0-dev

状态：普通地牢样本中的 `CardEngine` 全局回合、玩家抽牌延迟和累计威胁进度已作为一个事务完成跨进程恢复；公开 Getter、界面、跨帧验证和连续两轮恢复均通过。

## 三种“回合”概念

本阶段先拆开了此前混用的三个概念：

- 威胁进度由 `SpawnController.currentWaveAlertCounter` 累计。样本每个威胁大格含 3 个小格，共 3 个大格；累计值 5 表示第一大格已满，第二大格为 `2/3`。界面上方的 `2/3` 只表示当前大格内部进度。
- 敌人卡牌行动倒计时来自每张卡的 `getCurrentTurnCounter()`，属于单卡状态，不在本切片内。
- 玩家抽牌延迟来自 `BP_ControllerDeck_C:getTurnDrawDelay()`，表示距下一次回合抽牌还剩多少回合。

`CardEngine:getTurnCount()` 返回的全局累计值又是独立状态。旧原型只恢复这个值和威胁计数，因而报告 `verified` 时界面抽牌延迟仍可能错误；该判定已删除。

## 原生状态证据

游戏 EXE 门禁仍为大小 `82718720` 字节、SHA-256 `0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6`。反汇编和只读实机读取确认：

- `ACardEngine + 0x268` 指向 CardEngine 原生状态，`state + 0x18` 是 `getTurnCount()` 的权威值；
- `ControllerDeck + 0x220` 是其活动 `mCardEngine`；
- CardEngine 原生状态 `+0x48/+0x50` 是玩家 Deck 状态的共享对象和控制块；
- Deck 状态 `+0x68` 与 `+0x6C` 的有符号整数之和就是 `getTurnDrawDelay()`；
- `getTurnDrawDelay` 反射包装 RVA 为 `0x1027DC0`，对象解析函数 RVA 为 `0xF9F300`，最终求和 Getter RVA 为 `0xE323D0`。

所有原生访问均要求完整 EXE 指纹、函数地址和字节签名一致，并验证 Deck 归属活动 CardEngine、共享控制块引用计数稳定、地址可读写、原生和公开 Getter 相等。

## 写入探针

受保护探针在恢复后的第二小关读取到：

- 基础值 `5`；
- 修正量 `0`；
- Getter 与 UI 均为 `5`。

探针仅把 `+0x6C` 临时写为 `-1`。Getter 和 UI 同步变为 `4`，保持 1000 ms 后在对象、CardEngine、Deck 状态、共享控制块及临时值都未改变的条件下恢复为 `0`，Getter 和 UI 回到 `5`。报告 `draw-delay-write-probe-20260907-161022-439.json` 为 `passed`。

## 持久化与恢复事务

新补充文件 `route-c-exact-turn-progress.json` 使用独立 schema 1，并通过 Route C 负载校验和、EXE 指纹、关卡和 wave 与主检查点严格绑定。它保存：

- `cardEngineTurnCount`；
- `playerDrawDelay`；
- `waveAlertCounter`。

恢复时先等待目标原生 wave 稳定。全局回合和累计威胁只允许从新战斗的 `0/0` 状态写入；抽牌延迟不假设初始值为 0，而是保留实时基础值，仅计算达到保存 Getter 所需的修正量。这条路径只使用已经过探针的 `+0x6C` 字段。

三项在同一个受保护事务中写入，并立即通过两个 Getter和一个反射字段复核。下一帧再次要求：

- CardEngine、Deck 状态及共享控制块身份未变；
- Deck 基础值未发生并发变化；
- 全局回合、抽牌延迟和累计威胁全部等于目标。

只有三项同时满足才把 `exactTurnProgressStatus` 标为 `verified`。任一项失败时，仅在所有写入字段仍等于目标值或原值、且对象身份可证明未变的情况下回滚；否则报告并发失败，不继续宣称精确恢复。

## 实机验收

基线为 `cometDungeon1 / waveIndex=1`：

- `cardEngineTurnCount=14`；
- 玩家抽牌延迟 `3`；
- 累计威胁 `5`，即第一大格已满、第二大格 `2/3`。

完全结束游戏后从标题主菜单恢复，启动默认值为全局回合 `0`、抽牌延迟 `5`、累计威胁 `0`。恢复日志记录 `0→14 / 5→3 / 0→5`，报告最终目标和观察值均为 `14 / 3 / 5`，用户确认 UI 正确。

随后返回标题主菜单，在同一进程再次恢复。新 CardEngine 与 Deck 对象重新建立，第二份报告仍为 `passed / exactTurnProgressStatus=verified`，目标和观察值继续为 `14 / 3 / 5`；用户再次确认 UI 正确。这证明修正量不会跨恢复累积。

最终构建：

- DLL SHA-256：`CE20D0AA460DECD5D90BA0C66F56AE8B26F2D144069B2A934673205671228C0B`；
- 部署 ID：`20260908-002403-835`；
- 证据目录：`QuantumProtoclMod.runtime-evidence/20260908-v0150-turn-progress-restore`。

## 下一步

玩家 `FIELD` 现在是优先级最高的精确切片。必须把原生位置迁移扩展为完整事务，同时恢复槽位、当前生命、是否已攻击、单卡回合计数及墓地/手牌/牌库的整体一致性。敌人卡牌行动倒计时仍属于单卡状态，不应与本阶段的抽牌延迟或威胁进度合并。
