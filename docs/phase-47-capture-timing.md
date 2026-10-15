# 第四十七阶段：自动保存时钟记录候选

2026-10-15。v0.46.0候选已通过生产配置构建和5/5 CTest，已归档，**尚未部署或运行游戏**。现场仍是已实测的v0.45.0。此阶段只补下一次自动保存实验所需的时间记录，不补记PROCESSING分支已实测，也不扩大恢复范围。

## 改动

在已有自动请求的新调度、同波次合并、PROCESSING等待、延期、完成和拒绝日志中，追加同请求时钟信息。没有增加日志调用次数、原生读写、Pending字段或重试动作；原500ms延期、首次到期后30秒截止、身份校验和状态门禁保持原逻辑。

| 字段 | 口径 |
| --- | --- |
| `timingRequest`、`timingWave` | 此记录对应的自动请求与波次，避免与旧reason字段重名 |
| `clock=steady`、`clockPeriodNum/Den` | 原始steady-clock tick单位，不能当UTC或跨进程绝对时间 |
| `observedTicks` | 生成日志时的采样时间；不是准入判断发生的时间 |
| `readyTicks`、`deadlineTicks` | 当前保留的到期时刻和固定截止时刻 |
| `scheduleReadyTicks` | 仅新请求调度时记录的实际初始ready值 |
| `firstDueDerivedTicks`、`firstDueDerivation=deadline-minus-30s` | 从deadline减30秒派生，不能冒充独立初始观测 |

PID沿用原日志前缀。比较初始scheduleReady与后续deadline／derived值可以检查对应关系；完成日志可能在deadline之后写出，不能据此单独判定超时准入。

原日志每条只保留256字节事件。普通记录继续保持此上限，专用自动保存时钟记录最多1536字节，使用2048字节栈缓冲并保留前缀空间。新增拼接全部位于noexcept隔离中，格式化失败或超界时只回到一次原日志写入；不因写盘失败发起第二次I/O。日志仍为best-effort，缺失时钟字段不能证明没有重试或写入。

## 验证和现场状态

独立审阅核对唯一C++文件差异，未发现状态、native、重试或截止逻辑变化。生产OFF构建成功，5/5 CTest通过，开发夹具标记在ASCII及UTF-16中均不存在。没有新实机、没有新增日志故障注入或PROCESSING正例；这些仍按[续测方案](next-automatic-capture-validation.md)执行。

证据根 `QuantumProtoclMod.runtime-evidence/20261015-capture-timing`。已保存原源码、构建命令／日志、候选DLL、27个源码／构建文件快照及工作区patch；summary固定35个归档文件记录。构建验证SHA `42DC3C6CF212655840575AD7DC65757115E0B2917FD1E68DD8E1F5206DD53B3A`，summary SHA `433F6DEE908CB7FC7E09A9DEA5BBD690FB586C3FEF298F8845E8E3F5388FD179`。

候选DLL SHA `D1AA93DBF5CF4E5C47946B6C2730E942A8D9738BC5AEE0CED3C56362B8F746CB`，Mod源码 SHA `626C07AD195661540732FAD5DA45519EB580E1B1978A0B49D81A9563453C6F89`。现场23个文件及原数据目录文件集合与本片开始完全一致，游戏关闭，已部署DLL仍是v0.45.0 `D526BAEF456ED40FAB4B591B8676FECF4020D431988FE807CE223EDD883E5A13`。

下一次需先独立备份、核对候选源码／DLL并按正常部署流程测试，再观察真实自动请求。时钟记录补齐只让实验更容易核对，不能替代PROCESSING状态、无提示、原生队列图、生命周期和文件端点证据。
