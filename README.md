# BetterMacField

**0.4.0 · macOS launcher and graphics/input integration for Arknights: Endfield**

[English](#english) · [简体中文](#简体中文)

## English

BetterMacField adds keyboard/mouse integration and graphics controls to the iOS version of **Arknights: Endfield** running through **PlayCover** on Apple Silicon Macs. Version **0.4.0** adds custom keyboard settings, a 120 FPS override and missing scene vegetation restoration under Experimental. It targets **CN and global iOS 1.6.3**; CN is selected by default, and subsequent region choices are remembered.

### Features

| Page | Available controls |
| --- | --- |
| Graphics | 720p–2160p output; MetalFX Spatial by default, optional Temporal, and native TAAU last in the menu; 66.7% / 58.8% / 50% rendering scale; sharpening; mobile / PC medium / PC high scene LOD; vegetation density at game default / 50% / 70% / 100% |
| Lighting | Character self-shadows, character lighting and shadow filtering, shadow resolution, original GTAO, contact shadows, SSR and experimental PC character specular lighting |
| Input | Keyboard/mouse integration, sprint and combo input handling, tool wheel, camera zoom, interaction selection, optional 15 FPS background limit and Metal HUD (off by default) |
| Experimental | Custom keyboard bindings, 120 FPS override, missing scene vegetation restoration; CN and global iOS 1.6.3 are supported. PC character models and EFMI MOD remain placeholders. |
| Status & recovery | Client/component verification, runtime status, log copying and restoration of the PlayCover settings changed by this tool |

**⌘G** switches between native and enhanced graphics. By default, the native comparison also selects **TAAU at 50% rendering scale and mobile LOD**; either option can be disabled. Press again to restore the captured enhanced settings. Output resolution stays unchanged, and comparison mode does not overwrite saved preferences.

The launcher and in-game menu share their settings. CN and global settings are stored separately. The public version has **no UID hiding, MOD loading, model/texture replacement or extraction functionality**. Additional GI experiments and private development features are outside this release.

Vegetation density defaults to **100%** and adjusts existing grass and reeds through the game's native setting. It uses the installed mobile resources; selecting **Game default** restores the original density. Both the launcher and in-game menu expose this setting.

The [iOS resource reuse plan](docs/ios-resource-reuse.md) tracks the separate static-vegetation restoration candidate, its PC scene-data dependencies and the work needed for public integration.

The experimental **120 FPS** option changes both Unity's target frame rate and its display-link refresh request, lifting the original 60 FPS cap. **Game default** restores the captured gameplay cap. Background energy saving still requests 15 FPS. Actual frame rate depends on the display and scene workload; the global iOS 1.6.3 client has been verified rendering above 60 FPS. See [validation notes](docs/validation.md).

### Use

This document describes the 0.4.0 source. Packaged downloads and checksums are listed on the [Releases page](https://github.com/Shalistera/BetterMacField/releases).

1. Install a supported IPA through PlayCover. Enable **Block SleepSpamming** in the game's **Bypasses** settings, launch it successfully once through PlayCover, finish downloading the game data, then exit the game.
2. Open a prepared `BetterMacField.app` package. Read the injection-risk notice: “Use” unlocks after **10 seconds**, “Do not use” exits immediately, and “Do not show again” is optional.
3. Select the region and graphics settings, then launch the game. The packaged app requires no Python, Xcode or terminal.

The launcher finds PlayCover installations under:

```text
~/Library/Containers/io.playcover.PlayCover/Applications/
```

- CN: `com.hypergryph.endfield.app`
- Global: `com.gryphline.endfield.ios.app`

Scroll to zoom the camera. Hold **Option/Alt + scroll** to select interaction entries. Hold **R** to open the tool wheel. Exit the enhanced session and launch directly through PlayCover to use the original client.

BetterMacField adds another injected module on top of PlayCover and **may increase the risk of an account ban**. The launcher displays this notice before use.

**This project currently has no paid Apple Developer Program membership.** The app is ad-hoc signed, without Developer ID signing or Apple notarization. macOS may block its first launch.

### Current limits

- Defaults: **1080p, quality preset, MetalFX Spatial**, character lighting, GTAO and contact shadows. Existing user settings are preserved.
- Spatial can show motion trails. Temporal offers better image quality but may still have bugs. After multiple release fixes, the maintainer reports mostly trouble-free personal use.
- LOD changes follow the game's streaming and resource lifetime, so already-loaded scenery may not change immediately.
- Validation primarily used an M1 Max on macOS 27. All maps, characters and macOS versions have not been covered. See [validation notes](docs/validation.md).

### Build and source scope

Requirements: **Apple Silicon, macOS 13+, Xcode Command Line Tools and Python 3.9+**. The source runtime library can be compiled with:

```sh
python3 bettermacfield.py build
python3 -m unittest discover -s tests
```

A **complete launcher package requires prepared local rendering-program and native-assets vegetation exports**. Run `prepare_graphics.py` with the rendering export and `prepare_vegetation.py` with the local 1.6.3 research export before `build_release.py`. The latter needs Python `lz4`; its inputs are described in [the resource reuse notes](docs/ios-resource-reuse.md). Game-derived data stays under ignored `build/` and `dist/` directories. Player-side extraction tools are not part of this release.

```sh
python3 prepare_graphics.py /path/to/local-rendering-export
python3 build_release.py
# CN is the default; global is explicit:
python3 bettermacfield.py launch
python3 bettermacfield.py --region global launch
```

The build produces `dist/BetterMacField.app`, `dist/BetterMacField-macOS-arm64.zip` and `dist/SHA256SUMS.txt`. The launcher checks the client region, version and packaged components. Clients with different signatures can launch directly. If the game version differs, it offers **Launch anyway**. The packaged launcher's command-line equivalent is `--launch --force`. See [architecture](docs/architecture.md) and [usage notes](docs/quickstart.md) for details; these supporting documents are currently in Chinese.

### License

The project's launcher and integration source are released under [MIT](LICENSE). Game assets, original or adapted game shader programs, IPA files, Unity bundles and MOD payloads are excluded from the tracked source and are not relicensed under MIT. The application icon is included. See [NOTICE](NOTICE) for the boundary between project code and external components.

## 简体中文

BetterMacField 为通过 **PlayCover** 运行的《**明日方舟：终末地**》iOS 客户端补充 macOS 键鼠和画面设置。**0.4.0** 将修改键盘快捷键、120 帧解锁和缺失场景植被恢复加入实验性功能，适配国服、国际服 **iOS 1.6.3**。首次使用默认国服，之后记住用户选择。

### 功能

| 页面 | 可用功能 |
| --- | --- |
| 画面 | 720p–2160p；默认 MetalFX Spatial、可选 Temporal、菜单末位保留原版 TAAU；66.7% / 58.8% / 50% 渲染比例；锐化；手机 / PC 中档 / PC 高档场景 LOD；植被密度：游戏默认 / 50% / 70% / 100% |
| 光影 | 角色自阴影、角色光照与阴影滤波、阴影精度、原版 GTAO、接触阴影、SSR、实验性角色 PC 高光 |
| 操作 | 键鼠接入、冲刺与连续技输入处理、工具转盘、镜头缩放、交互项选择、可关闭的后台 15 FPS 限制、Metal HUD（默认关闭） |
| 实验性功能 | 修改键盘快捷键、120 帧解锁、缺失场景植被恢复；适配国服与国际服 iOS 1.6.3。角色高模和 EFMIMOD 保留占位入口。 |
| 状态与恢复 | 客户端及组件校验、运行状态、复制日志、恢复本工具修改的 PlayCover 设置 |

**⌘G** 切换原生／增强画面。默认同时回退为 **TAAU＋50% 渲染和手机 LOD**，这两项可以分别取消；再按一次恢复先前的增强设置。输出分辨率保持不变，对比状态不会覆盖保存的偏好。

启动器与游戏内菜单共用设置，国服和国际服分别保存。公开版**不包含隐藏 UID、MOD 加载、模型／贴图替换及提取功能**。额外 GI 实验和私人开发功能留在本地。

植被密度默认 **100%**，通过游戏原生参数调整当前场景已有草丛、芦苇等植被，使用已安装的移动端资源。启动器和游戏内菜单均可切换档位；选择「游戏默认」会恢复原有密度。

缺失场景植被恢复默认关闭，开关下次启动生效。模型、材质和贴图复用 iOS，场景摆放记录来自 PC，国服与国际服共用该场景包。数据依赖与验证情况另见[场景植被恢复与 iOS 资源复用](docs/ios-resource-reuse.md)。

实验性 **120 FPS** 同时调整 Unity 目标帧率和显示刷新请求，解除原来的 60 FPS 上限。「游戏默认」恢复接管前的游戏帧率；后台节能继续请求 15 FPS。实际帧率受屏幕和场景负载影响，国际服 iOS 1.6.3 已验证渲染帧率超过 60 FPS。详见[验证记录](docs/validation.md)。

### 使用

本文对应 0.4.0 源码；公开安装包及校验值见 [Releases](https://github.com/Shalistera/BetterMacField/releases)。

1. 在 PlayCover 安装对应版本 IPA，在游戏设置的「绕过」中开启 **Block SleepSpamming**，再从 PlayCover 成功运行一次游戏，下载完资料后退出。
2. 打开已打包的 `BetterMacField.app`，阅读注入风险提示。**10 秒**后可点击“使用”；“不使用”立即退出，可勾选“下次不提示”。
3. 选择服务器和设置，点击“启动游戏”。使用打包应用无需 Python、Xcode 或终端。

默认查找目录为：

```text
~/Library/Containers/io.playcover.PlayCover/Applications/
```

- 国服：`com.hypergryph.endfield.app`
- 国际服：`com.gryphline.endfield.ios.app`

滚轮缩放镜头；**Option／Alt＋滚轮**选择交互项；长按 **R** 打开工具转盘。退出增强会话后，直接从 PlayCover 启动即可使用原版。

BetterMacField 在 PlayCover 基础上额外注入增强模块，**可能增加账号被封禁的风险**，启动时会提示。

**本项目目前没有付费 Apple 开发者账号。** 应用采用本地签名，未使用 Developer ID 签名，也未经 Apple 公证；首次打开可能被 macOS 拦截。

### 当前限制

- 默认 **1080p、质量档、MetalFX Spatial**，开启角色光照、GTAO 和接触阴影；已有设置会保留。
- Spatial 可能出现移动拖影。Temporal 画面更好，但可能存在 bug；维护者反馈，正式版多次修复后的个人使用基本未遇到问题。
- LOD 变化遵循游戏的资源流送和释放，已加载的远景可能延迟变化。
- 主要验证环境是 M1 Max、macOS 27，尚未覆盖全部地图、角色及系统版本。详见[验证记录](docs/validation.md)。

### 构建与开源范围

需要 **Apple Silicon、macOS 13+、Xcode Command Line Tools、Python 3.9+**。运行模块可直接从源码构建：

```sh
python3 bettermacfield.py build
python3 -m unittest discover -s tests
```

**完整启动器需要本地准备的渲染程序与场景植被导出**。维护者先运行 `prepare_graphics.py` 和 `prepare_vegetation.py`，再运行 `build_release.py`；植被构建使用 Python `lz4`，输入格式见[资源复用说明](docs/ios-resource-reuse.md)。游戏衍生数据保存在被忽略的 `build/`、`dist/` 内。当前提取准备由维护者完成，玩家使用准备好的安装包。

```sh
python3 prepare_graphics.py /path/to/local-rendering-export
python3 build_release.py
# 默认国服；国际服需指定：
python3 bettermacfield.py launch
python3 bettermacfield.py --region global launch
```

产物为 `dist/BetterMacField.app`、`dist/BetterMacField-macOS-arm64.zip` 和 `dist/SHA256SUMS.txt`。启动器检查客户端区域、版本与包内组件；不同签名的客户端可直接启动，游戏版本不一致时提供「仍然启动」。打包后的启动器命令行可用 `--launch --force` 执行相同操作。详见[架构](docs/architecture.md)及[使用说明](docs/quickstart.md)。

### 许可证

本项目的启动器与集成源码采用 [MIT](LICENSE)。游戏美术资产、原版或适配后的游戏着色程序、IPA、Unity 资源包及 MOD 数据不进入源码仓库，也不转授为 MIT；应用图标随源码提供。具体边界见 [NOTICE](NOTICE)。
