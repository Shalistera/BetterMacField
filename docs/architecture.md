# 架构与版本适配

`bettermacfield.py` 负责精确客户端校验、本地编译、单进程启动和 PlayCover 设置恢复。客户端资源与模型工具不参与构建。`src/core.m` 将以下组件组成一个 arm64 动态库：

- `engine.inc`：IL2CPP API 绑定、原生参数系统与角色自阴影 Volume override。
- `input_*.inc`、`keyboard_input.inc`、HUD 与菜单模块：AppKit 事件接入现有游戏输入，沿用原始技能和交互回调。SDK/网页覆盖层出现时释放捕获。
- `display.inc`、`game_settings.inc`、`scene_lod.inc`：输出/内部渲染比例、16:9 窗口、原生设置页面与 Streaming 参数。
- `metal_hooks.inc`、`metal_library.inc`：在进程中识别现有 Metal library SHA-256，跟踪原始 TAAU 输入和后处理边界；不导出或替换游戏着色器。
- `native_taau_metalfx.inc`：原生 TAAU 插入点的 Temporal；运动矢量从游戏打包格式转换为 RG16Float，使用同一帧原始颜色和深度。
- `native_spatial.inc`：原生后处理之后、UI 合成之前的 Spatial，RGBA8Unorm perceptual 输入。
- `graphics_bridge.inc`、`gbuffer_retention.inc`、`native_temporal_size.inc`：经函数/布局校验的缓冲保留与输出尺寸适配，保留原生 C++ 光照。
- `metal_hud.inc`、`background_perf.inc`：系统 HUD 菜单、后台帧率。

## Temporal 的 jitter 修复

桥接使用原生 TAAU pass 的全局参数，而不是 managed Camera 的 jitter。GPU 同帧采样确认原始颜色在纹理坐标中的位移约为 `(-jitterX, -jitterY)`。旧桥接使用 `(+X, -Y)`，造成横向采样相位不匹配；修正为 `(-X, -Y)` 后，静止远景中的周期性横向晃动显著下降，用户动态试玩确认改善。

运动矢量仍使用原生数据，未用全部清零的诊断方案替代动态重投影。切换模式、分辨率或相机时重置历史，正常帧连续累积。Spatial 使用原版抗锯齿结果，当前已知有移动拖影。

## 客户端更新

当前公开版本只接受 README 标注的国际服 1.6.3 构建。更新时依次重新核对：

1. UnityFramework 和 global-metadata 的完整身份；禁止只改哈希强行通过。
2. IL2CPP 类型、参数类型、布局与现有回调的含义。
3. `graphics_bridge.inc`、`native_temporal_size.inc` 和键鼠 icall 包装器的预期代码/字段；每个本地绑定继续保留独立身份校验。
4. TAAU 与最终后处理的 Metal library 身份、纹理格式、尺寸、jitter 和运动矢量约定。
5. 在实际场景抽查自阴影开关、Temporal/TAAU 对照、键鼠释放与窗口尺寸。

本项目源文件里的函数指纹和布局信息是适配数据。MIT 覆盖本项目工具实现，不授予游戏客户端或提取资源的再分发权。模型/材质移植与额外光照着色器的生成流程在后续单独设计。
