# 客户端更新接入与发布清单

本页记录公开版的接入边界和验收要求。每次客户端更新在原文件补充目标版本、实际结果与未完成项；构建成功只证明构建，不替代进场验收。

## 0.5.0 版本矩阵

| 客户端 | 运行模块 | 已接入 | 尚未接入 |
| --- | --- | --- | --- |
| 国际服 1.7.2 / build 0 | `src/v172/graphics172.m` + `entry172.m` | 原生键鼠、滑索鼠标查询、窗口自由缩放、输出/LOD/草密度、角色光照、GTAO/接触阴影/SSR、原生 GI 与 MetalFX、⌘G 对比、后台节能 | 自定义键位页、120 FPS、缺失场景植被恢复 |
| 1.6.3、国服及其他版本 | 尚无专用适配 | — | 当前版本不提供适配 |

公开版范围以 README / NOTICE 为准。角色高模、衣装、模型与贴图替换、MOD、私人云层资源及提取功能不属于本表的回接目标。

## 每次更新必须核对

| 项目 | 检查内容与完成依据 |
| --- | --- |
| 客户端身份 | 记录 bundle ID、version、build、Unity UUID、metadata SHA；签名变化不当作代码变化。1.7.2 当前 UUID `0f92770a-6938-397f-a0f2-272e994ba286`，metadata SHA `3de06a6ee4f48cd920f2dd617f6a7ebb4755ba5308d9ad592b328a06c52def44`。 |
| 启动入口 | 原生 launcher、macabi entry、macOS graphics bundle 分别构建和签名；`--check` 校验完整 payload；首次部署和再次启动均确认运行的是公开副本。 |
| 运行协议 | 激活绑定实例、PID 和 session；过期状态不显示成功；设置应用等待对应回复；私人与公开目录隔离。 |
| API 与 hook | 重新解析 IL2CPP 类型/方法/字段；核对每个地址的实际指令与调用约定。入口失配应报告并关闭该路径。 |
| 键鼠 | 普通移动/攻击、滚轮与 Alt 交互选择、对话点击、滑索左键/右键分别检查；`GetKey`、`GetMouseButton`、InputSystem 状态一致。 |
| 窗口与输出 | 拖动边缘、分辨率切换、渲染比例、前后台与恢复；实际帧尺寸匹配设置。 |
| 角色光照 | 6 个角色 shader library 与原版参数一致；皮肤、身体、脸、布料在战斗/特效状态均检查。模型替换另属私人版验收。 |
| 原生场景 | 24 个场景 library，56 programs / 64 variant 引用；每个变体用真实 GI 参数索引保留 6 个体积输入；原生雾与环境光不被增强路径覆盖。 |
| GTAO / 接触阴影 | 16 / 6 个原版 kernel 与绑定匹配；摄像机绑定 100 项；运行计数增长且 GPU 错误为 0。移动地形使用深度重建法线。 |
| Temporal | 使用原生 AA2/TAAU 的 color/depth/packed motion/global jitter；motion 解码一致，原生 jitter 符号为 (-X,-Y)，避免再次执行 managed X 翻转。检查静止远景和移动、模式往返、相机变化。 |
| Spatial | 原版后处理之后、UI 之前超分；实际场景切换验证，不能用 Temporal 的结果代替。 |
| 地形深度 | 原生 depth 必须 Store 并 resolve；16 个 HGTerrainPS + DecalLit/Visibility 共 18 个消费者按各自 fragment 槽绑定；所有 AO 关闭时仍保留地形深度。 |
| 帧生命周期 | 输出替换仅限同一 command buffer / generation；尺寸或相机变化重建；退出、再次启动和开关超分后验证资源状态。 |
| 功能表 | 对照 `launcher/features.json` 逐项标记支持版本；未接入项从当前 UI 移除，保留其他已有偏好；启动器与游戏内菜单一致。 |
| 打包与边界 | `prepare_graphics172.py` 校验文件哈希及完整地形消费者；游戏派生输入只进 ignored build/dist；Git 中不得出现私人路径、诊断执行入口或游戏资源。 |
| 发布 | 完成自动检查、签名与包哈希检查、公开包实际启动；将实测与沿用结果分开写进 validation；发布源代码、ZIP、SHA256SUMS。 |

## 1.7.2 已修复回归的原因

- MetalFX 氛围变化：启用超分时切换到了缺失原生 GI 输入的渲染路径。当前通过原生 C++ / AA2 图执行，并按真实 shader 参数保留 GI。
- Temporal 水平抖动：原生链的 jitter 与 managed 路径符号处理不能叠加。当前使用原生 (-X,-Y)，关闭重复翻转。
- 部分地面材质消失：只补 Decal/Visibility 的深度消费者，漏掉 HGTerrainPS。当前 manifest 完整列出 18 项，打包时检查数量与地形槽一致性。
- 滑索左右键失效：设备收到输入而旧版按键查询未收到。相关 `GetKey` / `GetMouseButton` / `anyKey` 入口读取同一按键状态。

本次功能验证与发布状态见 [验证记录](validation.md)。未完成项留在矩阵中，下一次更新继续逐项追踪。
