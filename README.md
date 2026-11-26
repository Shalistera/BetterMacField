# BetterMacField

**0.6.0 · macOS launcher and graphics/input integration for Arknights: Endfield**

[English](#english) · [简体中文](#简体中文)

## English

BetterMacField adds native keyboard/mouse input and graphics controls to the **global and China (Hypergryph) iOS 1.7.2 clients** of **Arknights: Endfield**, running through PlayCover on Apple Silicon Macs. Version 0.6.0 adds the China client and a simpler launcher: four pages (Quality, Character, Controls, Advanced), Smooth / Recommended / Ultra quality presets, settings that save and apply immediately, and an in-game menu with the same layout.

**Both regions require their verified iOS 1.7.2 / build 0. CN support has been checked offline only and has not yet been verified in game.** The previous 1.6.3 runtime and resource package have been retired. Other client versions require their own adaptation.

### Features

| Area | Controls |
| --- | --- |
| Graphics | 720p–2160p output; MetalFX Spatial, Temporal and native TAAU; 66.7% / 58.8% / 50% render scale; sharpening; mobile / PC medium / PC high scene LOD; native grass density |
| Lighting | Character lighting and self-shadows, shadow resolution, original GTAO, contact shadows, SSR and optional character specular lighting |
| Input | Native keyboard/mouse, shared mouse-button queries for contextual actions including zipline controls, camera zoom, interaction selection and tool wheel |
| Desktop | Resizable window, optional background frame limit, Metal HUD, live settings and ⌘G graphics comparison |
| Maintenance | Client/code and package verification, current-session status, diagnostics and restoration of the PlayCover settings changed by this tool |

MetalFX preserves the original GI volume inputs, fog and terrain-depth consumers. **⌘G** temporarily compares native and enhanced graphics; render scale and LOD can be included in the comparison. Saved preferences remain intact. **⌘⌥P** opens the in-game menu.

Custom keybinding settings, a 120 FPS override and missing scene vegetation restoration await 1.7.2 adaptation. The public version does not contain UID hiding, MOD loading, model/texture replacement or asset extraction. Character high-poly models and private clothing fixes are outside this package.

### Use

The app and its checksum are available from [Releases](https://github.com/Shalistera/BetterMacField/releases).

1. Install your region’s iOS 1.7.2 through PlayCover. Enable **Block SleepSpamming** under Bypasses, launch successfully once and finish downloading game data, then exit.
2. Open `BetterMacField.app` and read the injection-risk notice. The packaged app requires no Python, Xcode or terminal.
3. If both clients are installed, choose Global or CN when opening the launcher. With one client installed it is selected automatically. Select graphics settings and launch. Close any other instance of the same game first.

The source is `com.gryphline.endfield.ios.app` (Global) or `com.hypergryph.endfield.app` (CN) in `~/Library/Containers/io.playcover.PlayCover/Applications`. Under `~/Library/Application Support/BetterMacField`, the private game copy is in `PlayCover` (Global) or `CN/PlayCover` (CN). Each region keeps its own preferences and restore record and reuses only its own game container.

Scroll to zoom; hold **Option/Alt + scroll** to select interaction entries; hold **R** for the tool wheel. Exit the enhanced session and launch through PlayCover to use the original client.

Defaults are 1080p, the quality preset and MetalFX Spatial, with character lighting, GTAO and contact shadows enabled. Existing preferences are preserved. Temporal uses the corrected native motion/jitter path. Spatial and other scenarios have their actual test coverage recorded in [validation](docs/validation.md).

The app is ad-hoc signed and has no Apple notarization. BetterMacField injects an additional module into PlayCover’s game process; the launcher displays the account-risk notice before use.

### Build and scope

Requirements: Apple Silicon, macOS 13+, Xcode Command Line Tools and Python 3.9+.

```sh
# Compile only the project runtime, without external rendering assets:
python3 bettermacfield.py build
python3 -m unittest discover -s tests

# Package a prepared local rendering export:
python3 prepare_graphics172.py /path/to/export-containing-character172-scene172-native172
python3 build_release.py
python3 bettermacfield.py check
python3 bettermacfield.py launch
```

Both regions share one verified rendering export; `--region cn` or `--region global` selects the client for CLI actions (for example, `python3 bettermacfield.py --region cn check`). The preparation helper verifies shaders, compute programs and binding metadata; it does not extract assets from a player installation. Game-derived inputs stay under ignored `build/Graphics172`, and the complete package is written to `dist/BetterMacField.app`, `dist/BetterMacField-macOS-arm64.zip` and `dist/SHA256SUMS.txt`.

The native launcher checks the bundle ID and build, Unity UUID, arm64 code-section and metadata SHA-256, and packaged file hashes. Re-signing is supported; unsupported code versions cannot bypass validation. See [architecture](docs/architecture.md), the [update checklist](docs/update-checklist.md) and [usage notes](docs/quickstart.md).

The launcher and integration source use [MIT](LICENSE). Game-derived rendering programs, game assets, IPA files, Unity bundles and MOD payloads are not relicensed under MIT. See [NOTICE](NOTICE).

## 简体中文

BetterMacField 为通过 PlayCover 运行的《明日方舟：终末地》**国际服及国服 iOS 1.7.2** 补充 macOS 原生键鼠和画面控制。**0.6.0** 接入国服，并简化启动器：画质／角色／操作／高级四页，流畅／推荐／极致画质预设，设置改动立即保存并应用，游戏内菜单使用同样的分区。

**两区均须使用对应的 iOS 1.7.2 / build 0。国服适配仅做离线核对，尚未进游戏验证。** 已移除 1.6.3 兼容运行库与旧资源包；其他版本需要单独适配。

### 功能

| 类别 | 可用功能 |
| --- | --- |
| 画面 | 720p–2160p 输出；MetalFX Spatial、Temporal、原版 TAAU；66.7% / 58.8% / 50% 渲染比例；锐化；手机 / PC 中档 / PC 高档场景 LOD；原生草密度 |
| 光影 | 角色光照、自阴影、阴影精度、原版 GTAO、接触阴影、SSR、可选角色高光 |
| 操作 | 原生键鼠、滑索等特殊操作的统一鼠标查询、镜头缩放、交互项选择、工具转盘 |
| 桌面 | 窗口自由缩放、可选后台限帧、Metal HUD、实时设置、⌘G 画面对比 |
| 维护 | 客户端代码与包内组件校验、当前会话状态、诊断信息、恢复本工具修改的 PlayCover 设置 |

开启 MetalFX 后保留游戏原生 GI 体积、雾和地形深度输入。**⌘G** 临时切换原生／增强画面，可选择同时比较渲染比例与 LOD；保存的偏好保持不变。**⌘⌥P** 打开游戏内菜单。

自定义键位页、120 FPS 解锁和缺失场景植被恢复尚待 1.7.2 适配。公开版不包含隐藏 UID、MOD 加载、模型／贴图替换或素材提取；角色高模与私人衣装修复不在此安装包内。

### 使用

安装包与校验值见 [Releases](https://github.com/Shalistera/BetterMacField/releases)。

1. 通过 PlayCover 安装所选区域的 iOS 1.7.2，在「绕过」设置启用 **Block SleepSpamming**，成功运行一次、下载完游戏资料后退出。
2. 打开 `BetterMacField.app` 并阅读注入风险提示。打包应用无需 Python、Xcode 或终端。
3. 只安装一服时自动选择；两服都已安装时，打开启动器后选择国际服或国服。调整画面设置后启动；先退出其他同服游戏进程。

源安装位于 `~/Library/Containers/io.playcover.PlayCover/Applications`：国际服为 `com.gryphline.endfield.ios.app`，国服为 `com.hypergryph.endfield.app`。首次启动会在 `~/Library/Application Support/BetterMacField` 下准备独立副本（国际服 `PlayCover`，国服 `CN/PlayCover`）。两服各有独立设置和恢复记录，只复用各自原游戏容器的下载资料和偏好。

滚轮缩放镜头；**Option／Alt＋滚轮**选择交互项；长按 **R** 打开工具转盘。退出增强会话后，直接从 PlayCover 启动即可使用原版。

默认 1080p、质量档、MetalFX Spatial，开启角色光照、GTAO 和接触阴影，保留已有偏好。Temporal 使用修正后的原生运动矢量与抖动路径。实际测试范围与限制见[验证记录](docs/validation.md)。

应用采用本地签名，尚未 Apple 公证。BetterMacField 在 PlayCover 基础上额外注入模块，启动器会在使用前显示账号风险提示。

### 构建与开源范围

需要 Apple Silicon、macOS 13+、Xcode Command Line Tools 和 Python 3.9+。

```sh
# 仅编译项目运行组件，无需游戏渲染资源：
python3 bettermacfield.py build
python3 -m unittest discover -s tests

# 使用已经准备好的本地渲染导出打包：
python3 prepare_graphics172.py /path/to/export-containing-character172-scene172-native172
python3 build_release.py
python3 bettermacfield.py check
python3 bettermacfield.py launch
```

两区共用同一份已核验的渲染导出。命令行用 `--region cn` 或 `--region global` 选择区域，例如 `python3 bettermacfield.py --region cn check`。准备脚本校验 shader、计算程序和绑定元数据，消费维护者已准备的导出。游戏派生输入保存在 Git 忽略的 `build/Graphics172`；产物为 `dist/BetterMacField.app`、`dist/BetterMacField-macOS-arm64.zip` 和 `dist/SHA256SUMS.txt`。

原生启动器校验 bundle ID、版本/build、Unity UUID、arm64 代码段与元数据的 SHA-256，以及包内文件哈希。支持不同签名，未适配代码不能跳过校验。参见[架构](docs/architecture.md)、[更新清单](docs/update-checklist.md)及[使用说明](docs/quickstart.md)。

启动器与集成源码采用 [MIT](LICENSE)。游戏派生着色程序、游戏美术资产、IPA、Unity 资源包和 MOD 数据不转授为 MIT；具体边界见 [NOTICE](NOTICE)。
