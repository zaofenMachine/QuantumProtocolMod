# 第四十一阶段：角色充能与许可状态的有界验证

2026-10-15，v0.42.0。继续“小关重开＋受限玩家恢复”，没有启动重编程或敌人精确恢复。本阶段解决此前充能数值相等、`isCharacterAbilityOk()` 却不一致的问题：新存档保存实际观测到的许可值，恢复时要求匹配，始终不写该许可位。

## 新证明与兼容范围

角色充能补充仍独立于 D/H/T/F 布局与 Q1/Q2 资格证明；后者通过不代表角色能力状态通过。新捕获使用 C3，保存 `observed-allowance-match-v1` 和规范字符串 `False`／`True`，绑定主存档 checksum、EXE、捕获时间、关卡、波次、充能与需求。只支持 `0 <= charge < requirement <= 10000`。满充、能力牌生命周期、是否实际可以发动、完整 UI／技能私有历史不在本阶段范围。

| 角色补充版本 | 保存时许可来源 | 恢复约束 |
| --- | --- | --- |
| C1 | 未保存，始终 unknown | 保留旧数值恢复语义；即使当前观测值相等，也不产生新许可证明 |
| C2 | `observed-disallowed-v1`，仅 False | 保留候选版本原有契约；恢复期间采样必须为 False |
| C3 | `observed-allowance-match-v1`，False 或 True | 恢复期间采样必须匹配保存值，不把 True 当满充或能力牌存在 |

C1 沿用原十字段 checksum 顺序和旧解析兼容性，但拒绝夹带新许可字段。C2/C3 严格限定十三个 JSON 字段，在旧哈希字段后依次追加 policy、allowance；不接受未知字段、非规范数字／布尔字符串或策略串版本错配。四个 checksum 基准分别为 C1 `7A85223F9B332D00`、C2 `7763319A95763E08`、C3 False `92A07E49B7829F34`、C3 True `866F850AE7C2D968`。

缺失、损坏、旧版未知、随布局禁用、已验证是不同结果。资源补充缺失／损坏时，DHTF 可在其自身范围内成功，但 `characterAllowanceProven=false`；不得将整个玩家状态称为精确恢复。新证明许可与重开实际许可不一致时，在玩家布局暂存／重建前失败，走既有一次语义回退；回退不会重新开启资源证明。

## 原生读取与恢复守卫

固定 EXE 中 getter 的原生主体是读取角色槽 `+0x254` 字节，并不根据充能或槽内卡牌数量计算。新增只读观测验证游戏线程、唯一 PLAYER 角色槽、槽／引擎／世界的归属和 weak identity，以及 `mCardEngine` 指针属性。三个 getter 的反射返回 ABI、完整 native thunk 和许可 getter 主体字节均有签名守卫；两次完整采样必须一致，规范 bool 返回还须与原生字节相符。

捕获在发布文件前再次核对同一 owner 与全部资源值。恢复在玩家布局前、数值增加前后、数值已经相等的分支、长弓回放后，以及最终稳定检查中重新核对。数值仍只通过原有公开 `addCharacterAbilityCharge` 正增量接口恢复；不调用许可 setter、不直接写字节、不清空能力牌、不增加修补重试。

运行时沿用三秒稳定期限并做离散采样；独立审计确认相应事件次序和最终数值，不把 trace 的两个时间戳当完整三秒连续观测。`through-stability` 表示运行时采样通过，未证明两次采样之间绝无变化。部分样本的可见 trace 起止间隔小于三秒，审计保留 `independentFullStabilityDurationProven=false`。生命周期更换、稳定期间许可漂移和满充拒绝有防御代码与离线检查，本阶段未取得这些故障的实机注入证明。

## 候选回归与最终修正

最初 build-03 的 C2 仅接受 False。实机正常加载 SaveGame 后自然出现 True、2/6；该候选跳过了资源保存，重开后成为 True、0/6，原比较只有 24/25，通过的 DHTF 报告不能抵消数值丢失。该失败保留在 `natural-true-resource-absent-restored`，没有更改原比较或补写来源证明。

最终 build-04 采用独立 C3 保存实际 False／True 并要求恢复匹配，避免将正常 True 来源无条件丢弃。正常 SaveGame 加载与主菜单直接 F6 的启动路径分别实际观测到 True 和 False；这只是本轮路径相关性，不等于已经证明全部初始化因果，也没有证实“曾经满充”假说。`getCardInstanceListSorted=()` 不能充当能力牌完整为空的证明。

## 实机矩阵

最终审计保留二十个资源案例，另外原用户检查点使用既有兼容审计。下表中的“通过”只指该行声明的范围；负例和未知来源不计为资源恢复正例。

| 类别 | 实际范围与结果 |
| --- | --- |
| 7 个资源正例 | build-03 C2 False 2/6；最终 build-04 对旧 C2 的兼容；C3 False 0/6、2/6；C3 True 0/6、2/6；C3 True 2/6 与 Apple FIELD 共存。均通过原25项及资源审计 |
| 2 个许可不匹配 | False 来源／True 启动及 True 来源／False 启动，均在玩家布局前拒绝；各一次语义回退 passed，资源 proven=false |
| 2 个无资源输入 | 候选 True 数值丢失、最终构建显式移除资源补充；均保留24/25和资源 unavailable，未算成功 |
| 2 个损坏资源输入 | 坏 checksum、有效 checksum 但捕获时间不绑定；实机拒绝资源层，均24/25，其他输入字节不变 |
| 7 个 C1 资源兼容 | 六个真正旧来源与一个明确标注的离线 C3→C1 输入。五个25/25，两个主菜单启动案例24/25；许可一律 legacy unknown |
| 原用户最终回归 | 使用原17个 checkpoint 文件，两次恢复采集，最终25/25；角色资源 C1、玩家资格 legacy，均未升级保存时证明 |

True／False 的 2/6 正例均观察到公开数值接口从0增加至2，调用后立即核对且许可保持原值；0/6 正例不调用增加接口。FIELD 共存指基础长弓仍在 HAND、普通 Apple 在 FIELD，不能解读为长弓 FIELD 已支持。同名长弓等既有范围以第四十阶段为准，本阶段未重新覆盖其所有组合。

所有原始28个比较项和 differences 原样保留，包括 semantic／enemy 比较实际为 True 的样本；不会统一改成 False 或豁免资源差异。资源审计器独立验证原始证据、源／目标 DLL、存档绑定、typed getter 与 trace；它本身不重算原25项，因此冻结复验另外运行原比较器。负例的输入文件改动有明确离线 fixture 清单，实际游戏恢复和合成审计自测分别计数。

## 构建与证据入口

生产 OFF 构建 4/4 CTest，七个 DEV 标记的 ASCII／UTF-16 缺席检查通过；没有声明 ON 构建通过。最终 DLL SHA256 `58867DE68C189005D287047FAAB2E30C25404F707FE7E9E8FAA06237355D863F`，实际 Mod 源码 `692079558207364BB16DC660B0DA2BACA642CD9B335F930F45715576BF57703B`。固定游戏 EXE 大小82718720、SHA256 `0DCF220317FA31667C14DD7FB41A6757B94FF7CDE2262E5A87337D00CCB017A6`。

证据根 `QuantumProtoclMod.runtime-evidence/20261015-character-allowance-guard`，分别保留 build-03 和 build-04 的实际源码／DLL、原始来源、恢复前输入副本、PID／EXE／DLL／helper 哈希、截图、报告、trace 与失败尝试。独立审计自测有10个合成正例、57个拒绝变体和四个 checksum 基准；它们不计为实机证据。上一阶段 A/B 冻结证据保持原样。

74个工具文件已冻结，固定2233个外部文件和152个目录集合，manifest SHA256 `53F536F0FD85CEFB72DCB24BCB58A892EA9BA3300DC6E1A2FFDC13D75AAE05F4`。`phase41-frozen-revalidation-20261014T222451560922Z/revalidation-index.json` 的44项复验通过：21项独立验收、22组全部28项／原始差异复算、1组合成自测。复验通过包含正确识别拒绝和24/25结果，不能解读成44次成功恢复。

本轮 fresh backup 共22文件（17个 checkpoint、4个 SaveGames、旧B DLL），SHA256 `717D8C1BE4081EABA210BC0E43E61F63C3B192E1E0102E8BCF56BCC38607F08B`。冻结清理工具独立检查完证据后正常关闭唯一验证游戏进程，等待实际父 launcher 自然退出；没有强杀。先备份清理前状态，再逐字节恢复21个原数据文件，原主 checksum `81095982711C71E2`，新58867D DLL保留。`session-cleanup.json` 为 completed、游戏进程已无，未操作 Genesis。

`validation-summary.json` SHA256 `EFDADFBFF9D76AF3852516DC04B0762E6DC8690F8DB66DD2B1AF4A8B58CB2B0D`；cleanup SHA256 `53A99257E141E5750F56A934ED511EF3D0785B2469633FE6AACD0131D82FA43E`。提交与远端核验另记 `commit-and-push.json`。后续独立测试可能再次启动游戏，以 WIP 最新入口为准；不得往本轮固定 case／source／build 目录新增文件或修改原始工具。

所有结论保持 `fullPlayerExactRestorationProven=false`，完整角色能力牌状态、敌人恢复、连续许可不变及完整 UI 历史均未证明。
