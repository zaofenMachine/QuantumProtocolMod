# 文档索引

以下三份文件是后续维护时应优先更新的权威文档：

- [requirements.md](requirements.md)：用户需求、检查点语义、未决产品问题。
- [architecture-options.md](architecture-options.md)：小关开头与重编程锚点两条路线的成本和取舍。
- [technical-status.md](technical-status.md)：已验证事实、失败实验、API 线索和当前技术边界。
- [cpp-development.md](cpp-development.md)：C++ 工程、构建依赖、只读导出器和 SDK 生成流程。
- [sdk-analysis.md](sdk-analysis.md)：生成的 Quantum CXX 头文件、玩家/敌人状态边界和最新路线判断。

阶段报告保留实验发生时的上下文与证据：

- [phase-1-feasibility.md](phase-1-feasibility.md)：第一阶段可行性调查。
- [phase-2-hot-reload-crash.md](phase-2-hot-reload-crash.md)：`Ctrl+R` 与原 `Ctrl+F4` 崩溃分析。
- [phase-2-player-reset-test.md](phase-2-player-reset-test.md)：玩家牌区清空与原生重载测试。
- [phase-2-reprogram-anchor.md](phase-2-reprogram-anchor.md)：重编程锚点实验小结。
- [phase-3-cpp-readonly-export.md](phase-3-cpp-readonly-export.md)：C++ getter、精确槽位、效果与计数器导出验证。
- [phase-4-native-card-state.md](phase-4-native-card-state.md)：游戏私有卡牌状态布局、EXE 指纹与复杂样本交叉验证。
- [phase-5-guarded-health-write.md](phase-5-guarded-health-write.md)：带版本门禁的临时生命写入、失败扫描路径与自动回滚验证。
- [phase-6-guarded-turn-write.md](phase-6-guarded-turn-write.md)：回合计数私有字段、跨帧 UI 同步、并发变化保护与自动回滚验证。
- [phase-7-route-c-vertical-slice.md](phase-7-route-c-vertical-slice.md)：普通地牢小关语义检查点的跨进程格式、恢复事务、安全门与真实验收步骤。
- [phase-8-exact-state-gap.md](phase-8-exact-state-gap.md)：同波次恢复前后语义差异、离线比较工具，以及首个精确 `spawnList` 补充切片的回滚与实机验证。
- [phase-9-fixed-player-zones.md](phase-9-fixed-player-zones.md)：`FDecklist.fixedOrder` 反汇编、精确初始牌库/手牌补充层、崩溃与时序修正，以及主菜单最终验收。

维护规则：需求发生变化时先更新 `requirements.md`；技术实验产生新证据时更新 `technical-status.md` 并补充对应阶段报告；正式选定恢复路线后在 `architecture-options.md` 记录决定与理由。
