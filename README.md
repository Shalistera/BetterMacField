# BetterMacField

**0.3.5 · macOS launcher and graphics/input integration for Arknights: Endfield**

[English](#english) · [简体中文](#简体中文)

## English

BetterMacField adds keyboard/mouse integration and graphics controls to the iOS version of **Arknights: Endfield** running through **PlayCover** on Apple Silicon Macs. This source release covers the **0.3.5 launcher** and its runtime integration. It supports the pinned **CN and global iOS 1.6.3 builds**; CN is selected by default, and subsequent region choices are remembered.

### Features

| Page | Available controls |
| --- | --- |
| Graphics | 720p–2160p output; native TAAU, MetalFX Spatial and experimental Temporal; 66.7% / 58.8% / 50% rendering scale; sharpening; mobile / PC medium / PC high scene LOD |
| Lighting | Character self-shadows, character lighting and shadow filtering, shadow resolution, original GTAO, contact shadows, SSR and experimental PC character specular lighting |
| Input | Keyboard/mouse integration, sprint and combo input handling, tool wheel, camera zoom, interaction selection, optional 15 FPS background limit and Metal HUD |
| Experimental | Greyed “Replace with PC character high-detail models” and “Use EFMI MOD” placeholders; clicking either shows “Not available yet” |
| Status & recovery | Client/component verification, runtime status, log copying and restoration of the PlayCover settings changed by this tool |

**⌘G** switches between native and enhanced graphics. By default, the native comparison also selects **TAAU at 50% rendering scale and mobile LOD**; either option can be disabled. Press again to restore the captured enhanced settings. Output resolution stays unchanged, and comparison mode does not overwrite saved preferences.

The launcher and in-game menu share their settings. CN and global settings are stored separately. The public version has **no UID hiding, MOD loading, model/texture replacement or extraction functionality**. Additional GI experiments and private development features are outside this release.

### Use

Download the app ZIP from [Releases](https://github.com/Shalistera/BetterMacField/releases/tag/v0.3.5). `SHA256SUMS.txt` is provided alongside it.

1. Install a supported IPA through PlayCover, launch it once, finish downloading the game data, then exit the game.
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

- Defaults: **1080p, quality preset, Spatial with the game's native TAAU antialiasing**, character lighting, GTAO and contact shadows. Existing user settings are preserved.
- The current Spatial combination has motion trails. Temporal is experimental; ANE assertion failures have occurred on macOS 27, and their cause remains unresolved.
- LOD changes follow the game's streaming and resource lifetime, so already-loaded scenery may not change immediately.
- Validation primarily used an M1 Max on macOS 27. All maps, characters and macOS versions have not been covered. See [validation notes](docs/validation.md).

### Build and source scope

Requirements: **Apple Silicon, macOS 13+, Xcode Command Line Tools and Python 3.9+**. The source runtime library can be compiled with:

```sh
python3 bettermacfield.py build
python3 -m unittest discover -s tests
```

A **complete graphics launcher package also requires a prepared local rendering-program export**. Those game-derived shader programs and binding files are not committed to this repository. An independent extraction workflow for arbitrary player installations is not included in 0.3.5.

```sh
python3 prepare_graphics.py /path/to/local-rendering-export
python3 build_release.py
# CN is the default; global is explicit:
python3 bettermacfield.py launch
python3 bettermacfield.py --region global launch
```

The build produces `dist/BetterMacField.app`, `dist/BetterMacField-macOS-arm64.zip` and `dist/SHA256SUMS.txt`. The launcher checks exact client and payload identities and rejects unsupported client updates. See [architecture](docs/architecture.md) and [usage notes](docs/quickstart.md) for details; these supporting documents are currently in Chinese.

### License

The project's launcher and integration source are released under [MIT](LICENSE). Game assets, original or adapted game shader programs, IPA files, Unity bundles and MOD payloads are excluded from the tracked source and are not relicensed under MIT. The application icon is included. See [NOTICE](NOTICE) for the boundary between project code and external components.

## 简体中文

BetterMacField 为通过 **PlayCover** 运行的《**明日方舟：终末地**》iOS 客户端补充 macOS 键鼠和画面设置。本次开源范围为 **0.3.5 启动器及其运行模块**，适配国服、国际服 **iOS 1.6.3 的指定构建**。首次使用默认国服，之后记住用户选择。

### 功能

| 页面 | 可用功能 |
| --- | --- |
| 画面 | 720p–2160p；原版 TAAU、MetalFX Spatial、实验性 Temporal；66.7% / 58.8% / 50% 渲染比例；锐化；手机 / PC 中档 / PC 高档场景 LOD |
| 光影 | 角色自阴影、角色光照与阴影滤波、阴影精度、原版 GTAO、接触阴影、SSR、实验性角色 PC 高光 |
| 操作 | 键鼠接入、冲刺与连续技输入处理、工具转盘、镜头缩放、交互项选择、可关闭的后台 15 FPS 限制、Metal HUD |
| 实验性功能 | “替换PC版角色高模”“使用EFMIMOD”两个灰色占位按钮，点击显示“暂未开放” |
| 状态与恢复 | 客户端及组件校验、运行状态、复制日志、恢复本工具修改的 PlayCover 设置 |

**⌘G** 切换原生／增强画面。默认同时回退为 **TAAU＋50% 渲染和手机 LOD**，这两项可以分别取消；再按一次恢复先前的增强设置。输出分辨率保持不变，对比状态不会覆盖保存的偏好。

启动器与游戏内菜单共用设置，国服和国际服分别保存。公开版**不包含隐藏 UID、MOD 加载、模型／贴图替换及提取功能**。额外 GI 实验和私人开发功能留在本地。

### 使用

从 [Releases](https://github.com/Shalistera/BetterMacField/releases/tag/v0.3.5) 下载应用 ZIP，同页提供 `SHA256SUMS.txt` 校验文件。

1. 在 PlayCover 安装对应版本 IPA，启动游戏下载完资料后退出。
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

- 默认 **1080p、质量档、Spatial＋原生 TAAU 抗锯齿**，开启角色光照、GTAO 和接触阴影；已有设置会保留。
- 当前 Spatial 组合存在移动拖影。Temporal 为实验选项，macOS 27 上曾出现 ANE 断言退出，根因尚未解决。
- LOD 变化遵循游戏的资源流送和释放，已加载的远景可能延迟变化。
- 主要验证环境是 M1 Max、macOS 27，尚未覆盖全部地图、角色及系统版本。详见[验证记录](docs/validation.md)。

### 构建与开源范围

需要 **Apple Silicon、macOS 13+、Xcode Command Line Tools、Python 3.9+**。运行模块可直接从源码构建：

```sh
python3 bettermacfield.py build
python3 -m unittest discover -s tests
```

**完整光影启动器还需要本地准备的渲染程序导出**，其中游戏衍生的着色程序和绑定文件没有提交到仓库。0.3.5 尚未提供面向任意玩家安装目录的独立提取流程。

```sh
python3 prepare_graphics.py /path/to/local-rendering-export
python3 build_release.py
# 默认国服；国际服需指定：
python3 bettermacfield.py launch
python3 bettermacfield.py --region global launch
```

产物为 `dist/BetterMacField.app`、`dist/BetterMacField-macOS-arm64.zip` 和 `dist/SHA256SUMS.txt`。启动器精确校验客户端与组件，客户端更新后会拒绝加载不匹配的适配。详见[架构](docs/architecture.md)及[使用说明](docs/quickstart.md)。

### 许可证

本项目的启动器与集成源码采用 [MIT](LICENSE)。游戏美术资产、原版或适配后的游戏着色程序、IPA、Unity 资源包及 MOD 数据不进入源码仓库，也不转授为 MIT；应用图标随源码提供。具体边界见 [NOTICE](NOTICE)。
