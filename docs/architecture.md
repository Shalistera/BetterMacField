# 架构与版本适配

当前唯一目标为国际服 iOS 1.7.2 / build 0。1.6.3 运行模块和资源包已移除，历史实现保留在 Git 历史。

`launcher/main.m` 负责原生设置界面、客户端/组件校验、运行反馈和 PlayCover 设置恢复。`launcher/features.json` 是启动器与游戏菜单共用的功能表。设置保存在 `~/Library/Application Support/BetterMacField/settings-global.json`；实时命令只发送运行时支持的字段，回复与 PID/session/request ID 对应。

`launcher/current172.inc` 校验 Unity UUID、metadata SHA 与构建号后，通过 APFS clone 创建隔离的 PlayCover 游戏副本，装载 `BetterMacField172.dylib`。macabi entry 校验激活实例后加载 macOS `BetterMacFieldGraphics.bundle`，状态位于原游戏容器的 `Data/tmp/BetterMacField/graphics172-*.json`。公开版与私人版各自管理激活、状态和工具设置，游戏下载资料与游戏偏好沿用原容器。

`src/v172` 包含独立的运行组件：

- `graphics172.m` 与 `unity_objects172.inc`：IL2CPP 接入、运行配置和通用 Unity 对象辅助。
- `input172.inc`：原生键鼠与共享鼠标状态查询，包含滑索操作入口。
- `metalfx172.inc`、`metalfx_native172.inc`、`native_temporal172.inc`：原生 AA2/TAAU 链上的 Temporal 与后处理后的 Spatial。
- `native_temporal172_size.inc`：验证 Unity UUID、磁盘/内存指令和字段布局后接入原生渲染回调，处理真实输出尺寸及相机生命周期。
- `native_metal172_hooks.inc`、`managed_gbuffer_inputs.inc`：保留原生 GI 和深度，限制输出替换到同一 command buffer 与 generation；按各自 fragment 槽提供地形、Decal、Visibility 深度。
- `scene172.inc`、`scene172_control.inc` 与 GTAO/contact/SSR helpers：真实 shader 身份、绑定和场景光影。
- `live172.inc`、`menu172.inc`、`lod172.inc`、`quality172.inc`：共享设置、⌘G 临时对比、LOD、画质和桌面行为。

MetalFX 继续使用原生 C++ 渲染图及原版 GI、雾、抗锯齿与后处理。Temporal 消费原生 packed motion 和 jitter，避免与 managed 路径重复翻转。地形深度 resolve 独立于 AO 开关，manifest 完整覆盖 18 个消费者。

`prepare_graphics172.py` 接受已准备的 `character172`、`scene172`、`native172`，仅导入清单引用且哈希相符的 shader、kernel 与绑定元数据。`build_release.py` 构建 macabi entry、macOS graphics bundle 和 launcher，生成 payload 哈希与签名 ZIP。运行组件可以独立编译，完整包还需要游戏派生渲染输入；玩家无需 Python/Xcode。

维护者 Python CLI 委托原生 launcher 执行检查、启动、状态与恢复，避免两套部署实现。当前接入状态与回归原因见[更新清单](update-checklist.md)，实际结果见[验证记录](validation.md)。
