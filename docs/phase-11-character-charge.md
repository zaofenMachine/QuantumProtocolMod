# 第十一阶段：角色充能精确恢复

日期：2026-09-05

版本：v0.13.0-dev

状态：未满角色充能已作为独立精确补充层完成保存、恢复、Getter 复核、UI 验收和恢复前后语义对比。满充能仍因可能生成能力卡而明确排除。

## 切片边界

复杂战斗基线证明路线 C 会把角色特殊资源从 `4/6` 重置为 `0/6`。游戏公开提供 `CardEngine:addCharacterAbilityCharge(int)`，因此本阶段只实现可以通过原生正向增量完成、且不会跨越满充触发点的最小切片：

- 只接受 `0 <= charge < requirement` 且需求值为正数；
- 保存到独立 schema 1 文件 `route-c-exact-character-charge.json`；
- 文件绑定主 Route C 负载校验和、游戏 EXE 指纹、关卡名和波次；
- 补充缺失、损坏或与主检查点不匹配时安全降级，不阻止普通路线 C 恢复；
- 恢复时需求值必须与实时 Getter 一致；实时充能高于目标则拒绝写入；
- 只调用公开 `addCharacterAbilityCharge` 增加差值，下一观察周期必须由 Getter 读回目标值才标记 `verified`。

满充能 `charge == requirement` 不进入此格式，因为调用公开 API 达到阈值可能同时创建角色能力卡。该副作用需要把能力牌的归属、牌区和重复生成保护一起验证，不能混入本切片。

## 持久化与报告

补充格式包含独立 FNV-1a 负载校验。单元测试覆盖：

- `4/6` 序列化、解析和校验和往返；
- `6/6` 满充能拒绝；
- 篡改充能值后因校验和不匹配而拒绝。

恢复报告新增：

- `exactCharacterChargeSupplementPresent`；
- `exactCharacterChargeStatus`；
- `exactCharacterChargeReason`。

写入状态区分 `pending`、`applied`、`verified`、`failed-no-write` 和 `failed-after-write`，持久轨迹记录加载、调用参数和读回结果。

## 实机验收

测试在 `cometDungeon1 / waveIndex=2` 保存，基线生命为 `3/8`、角色充能为 `4/6`。恢复时画面先显示原生 `8/8`、`0/6`，随后分别恢复为 `3/8`、`4/6`；用户确认表现正常。

最终报告 `route-c-restore-20260905-144136-908.json` 为 `passed`：

- `exactSpawnPlanStatus=verified`；
- `exactCharacterChargeStatus=verified`；
- `characterCardCharge=4`、`characterCardChargeRequirement=6`；
- `targetHealth=3`；
- 复杂场面不符合纯净牌区条件，旧 `exact-player-zones` 补充因主检查点校验和不匹配而按预期标记 `unavailable`。

持久轨迹显示恢复后初值为 `0/6`，调用公开 API 的 `amount=4`，下一周期读回 `4/6` 并标记 `verified`。恢复前后清单比较结果：

- `healthEqual=True`；
- `characterResourceEqual=True`；
- `futureSpawnPlanEqual=True`；
- `playerCardIdentityMultisetEqual=True`。

仍有 9 组复杂战斗差异，集中在引擎回合、Spawner 警戒计数、玩家牌区与单卡运行态、敌人场面与墓地；这与本切片边界一致。

最终构建：

- DLL SHA-256：`54D2CC98BA91211C5F0D1A1D5D7608453C254CF2D6FE45B4BAB24CD36AB75AB7`
- 部署 ID：`20260904-230624-517`
- 证据目录：`QuantumProtoclMod.runtime-evidence/20260905-v0130-character-charge`

## 下一步

下一切片优先研究玩家场上与墓地。它能同时回答卡牌位置、受伤值、已攻击状态以及是否能在路线 C 原生初始化后安全重建的问题。敌人状态还涉及波次完成判定、效果和行动队列，风险更高，应在玩家侧权威重建与回滚边界明确后再进入写入实验。
