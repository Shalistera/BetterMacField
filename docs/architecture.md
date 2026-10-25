# 架构与版本适配

原生 Launcher `launcher/main.m` 负责区域选择、设置页、客户端/组件校验、临时注入、运行反馈和 PlayCover 设置恢复。`launcher/features.json` 是 Launcher 与游戏菜单共用的功能表，避免两套 UI 丢失选项。每服分别保存偏好、命令与状态，更新命令带独立 ID；界面等待对应回复，避免旧状态覆盖刚保存的选择。

客户端按游戏标识和版本识别，版本不同时可选择继续启动。不同签名会改变客户端文件 SHA-256，因此客户端启动兼容性按标识和版本处理。SHA-256 用于 BetterMacField 包内组件完整性和渲染程序识别。超分选项按 Spatial、Temporal、TAAU 排列，默认使用 Spatial，并保留用户保存的选择。

植被密度由 `src/menu.inc` 调整游戏的 `GrassGlobalSparsity|Grass` 参数，复用已安装移动端资源。四档设置通过共享功能表进入启动器、游戏菜单和每服偏好；运行状态回读实际密度。选择游戏默认时恢复参数原值。此功能的交付内容为参数控制源码。

`src/core.m` 组合以下运行组件：

- `engine.inc`：IL2CPP API、原生参数与自阴影 Volume。
- 输入、键盘、HUD 模块：将 AppKit 事件送入原游戏逻辑，SDK/网页覆盖层释放捕获。
- `display.inc`、`game_settings.inc`、`scene_lod.inc`：输出/内部渲染比例、16:9 窗口、原生设置和场景流送。
- `graphics_runtime.inc`、`metal_library_trace.inc`、`metal_bindings.inc`：按实际 library SHA-256 识别图形程序和输入；绑定经过适配的光影程序。
- `managed_gbuffer_inputs.inc`、`graphics_mode.inc`、`native_temporal_size.inc`：保留有效深度与场景输入，执行前分别验证指令和字段布局。
- `official_gtao_runtime.inc`：原版 GTAO 程序、参数与历史帧。移动端地形预通道未写材质法线，改用游戏自带深度重建法线变体解决方块闪烁。
- `official_contact_runtime.inc`：原版接触阴影程序与当前场景光源。
- `character_skin.inc`、`metal_ssr_history.inc`：匹配角色着色与屏幕空间反射输入，不替换模型、贴图。
- `native_taau_metalfx.inc` / `native_spatial.inc`：原生 TAAU 输入上的 Temporal，以及原版抗锯齿之后的 Spatial。Temporal 的采样相位使用已经验证的 (-X, -Y) 符号。
- `settings.inc`、`menu.inc`：偏好、实时命令、一键对比快照；对比状态不写入用户配置。
- `metal_hud.inc`、`background_perf.inc`：系统 HUD 与可关闭的后台帧率限制。

`prepare_graphics.py` 只接受已校验的渲染程序导出，输出到 Git 忽略的构建目录；`build_release.py` 将它和动态库、功能表打包并生成组件哈希。源码许可证与该本地输入分别处理，见 NOTICE。运行时所有文件复制到所选客户端自己的容器，着色器清单的路径在复制时重定位。

更新适配依次核对客户端版本与构建、IL2CPP 类型与参数、预期指令与字段偏移、着色器身份/绑定、TAAU 的颜色/深度/运动矢量/jitter约定，最后在实际场景抽查输入、光影、超分与对比往返。国服与国际服使用各自的游戏标识和必要的区域 RVA。
