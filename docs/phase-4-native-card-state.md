# 第四阶段：原生卡牌状态定位与只读验证

## 目标

确认 `BP_InGameCard_C` 的公开 getter 背后是否存在独立的权威运行时状态对象，并在普通与复杂战斗样本中验证其生命值和回合计数布局。本阶段只读，不修改游戏状态。

这些偏移只适用于以下已测试游戏可执行文件：

- 文件：`Quantum-Win64-Shipping.exe`
- 大小：`82,718,720` 字节
- SHA-256：`0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6`
- PE image base：`0x140000000`

## v0.4 / schema 4：getter 调用链

v0.4 在报告中加入 `UFunction::GetFuncPtr()` 地址。实测所有相关 getter 均落在游戏主 EXE 中；以本次运行模块基址 `0x7FF6E4440000` 计算，关键 UFunction thunk RVA 包括：

- `getCardInfoInstance`：`0x102D020`
- `getCardLocation`：`0x102D090`
- `getCurrentCounters`：`0x102D0C0`
- `getCurrentHealth`：`0x102D100`
- `getCurrentTurnCounter`：`0x102D130`
- `getId`：`0x102D1B0`
- `getTag`：`0x102D5F0`
- `getPlacedFieldSlot`：`0x1020240`
- `getPlacementInfo`：`0x1027C10`

离线反汇编表明这些 thunk 会继续调用游戏私有实现。例如 `getCurrentHealth` 进入 RVA `0xF98890`，`getCurrentTurnCounter` 进入 `0xF988F0`。多个实现都会先读取 `AInGameCard + 0x228`，得到一份独立的原生卡牌状态对象。

在该对象中已识别：

- `state + 0x18`：卡牌 GUID。
- `state + 0x38`：`CardInfoInstance`。
- `state + 0xF0`：卡牌标签。
- `state + 0x118`：基础生命值。
- `state + 0x11C`：当前生命值。
- `state + 0x194`：回合调整量。
- `state + 0x198`：回合基础值；公开当前回合计数为该值与调整量之和。

额外反汇编证据：重置逻辑 RVA `0xE3E330` 会把 `state + 0x118` 复制到 `state + 0x11C`；伤害和治疗逻辑分别对当前生命执行扣减、归零或上限钳制。RVA `0xE522E0` 还存在一个仅写入 `state + 0x11C` 的私有小函数，但尚未由 Mod 调用。

## v0.5 / schema 5：运行时交叉验证

v0.5 只读导出以下诊断字段：状态对象地址、基础/当前生命、回合基础值、调整值和两者计算结果。每张卡仍同时通过反射调用公开 getter，从而可以逐张交叉核对。

### 普通样本

- 报告：`battle-inventory-20260830-162801-945.json`
- 相关对象：100 个。
- 活动卡牌：10 张。
- 10/10 张卡的私有当前生命与 `getCurrentHealth()` 一致。
- 10/10 张卡的私有回合计算结果与 `getCurrentTurnCounter()` 一致。
- 敌方 `enemyCacheRandom` 同时读到基础/当前生命 3、回合调整值 7、公开回合计数 7。
- 游戏无崩溃。

### 复杂样本

玩家在同一流程中经过两个小关和路线选择，额外编辑过一次卡组；样本包含场上、墓地、手牌、牌库和缓存区卡牌，三个敌人，敌我双方受伤卡牌，以及已变化的敌方回合计数。

- 报告：`battle-inventory-20260830-163337-284.json`
- 相关对象：141 个。
- 活动卡牌：17 张。
- 区域分布：敌方场上 3；玩家场上 3、墓地 1、缓存区 2、手牌 1、牌库 7。
- 17/17 张卡的私有当前生命与公开 getter 一致，零差异。
- 17/17 张卡的私有回合计算结果与公开 getter 一致，零差异。
- 玩家 `naturalLemon`：基础生命 2、当前生命 1。
- 敌方 `enemyInterceptor`：基础生命 4、当前生命 1。
- 两张 `enemyArtillery`：基础/当前生命均为 5，回合调整值与公开计数均为 4。
- 六张场上卡均可通过 `CardPlacementComponent` 关联到精确的阵营、前后排和索引槽位。
- 游戏无崩溃。

缓存区控制器返回 `naturalCherry` 与 `genericConstantCorrection`；牌库控制器返回编辑后的 7 张牌，证明运行时区域读取能够反映额外卡组编辑后的实际状态。

## 结论与边界

目前可以把 `AInGameCard + 0x228` 指向的对象判定为卡牌核心权威运行时状态，而不是单纯的界面显示对象。生命与回合偏移已在普通、受伤、多敌人、多牌区和跨小关样本中得到公开 getter 的独立交叉验证。

这仍不等于恢复已经实现：

- 偏移与私有函数均绑定当前 EXE 指纹，游戏更新后必须拒绝使用，不能静默沿用。
- 尚未进行任何私有状态写入，也未验证写入后 UI、动作队列、死亡判定和后续 AI 行为是否同步。
- 通用/特殊计数器显示对象在本次报告中的 getter 仍为 0；敌方可见倒计时已经由卡牌原生回合字段覆盖，但其他非零计数器仍需单独定位。
- 持续效果执行器、特殊 Spawner、Boss/教程辅助状态仍可能需要额外保存。

下一步采用可丢弃流程中的受控写入脉冲：只选择一张仍存活且已受伤的场上卡，在单次调用内部临时改变当前生命，通过公开 getter 验证后立即恢复原值，并生成独立报告。只有这个可回滚实验成功，才考虑让修改跨帧存在并研究 UI 刷新。
