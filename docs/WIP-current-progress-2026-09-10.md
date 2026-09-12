# QuantumCheckpoint 当前工作进度（临时续接记录）

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
