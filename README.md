# BetterMacField

为通过 PlayCover 运行的《明日方舟：终末地》iOS 客户端补充 macOS 操作、角色阴影与画面控制。支持 **国际服与国服 iOS 1.6.3 的指定构建**，提供源码和无需 Python/Xcode 的原生启动器。

## 当前功能

| 功能 | 实现与状态 |
| --- | --- |
| 角色自阴影 | 通过游戏原生 `HGCharacterVolume` 打开角色自阴影质量门槛，使用现有角色与阴影渲染流程 |
| 阴影精度 | 游戏默认、1024、2048；同时调整场景级联阴影和角色阴影图参数 |
| MetalFX Temporal | 接入原生 C++ 渲染路径，保留原有环境光；已修正 TAAU 输入与 MetalFX 横向 jitter 符号不一致造成的抖动 |
| MetalFX Spatial | 保留为备选，位于原版抗锯齿、Bloom、色调映射之后；当前组合仍有移动拖影 |
| 分辨率与超分档位 | 720p / 1080p / 1440p / 2160p 输出；质量 66.7%、平衡 58.8%、性能 50% 的宽高比例 |
| LOD 加载距离 | 手机默认、PC 中档、PC 高档；沿用游戏原生 Streaming 加载与释放逻辑 |
| 键鼠与桌面 HUD | 场景内收到键鼠输入后切换；自由视角、Alt 释放、WASD、攻击/闪避、技能、交互、菜单快捷键和工具转盘 |
| 窗口与 UI | PlayCover 2× UI 缩放；分辨率菜单驱动 16:9 窗口，超出桌面时按可用面积缩小 |
| Metal HUD | 调用系统 HUD；完整类型、位置与配置菜单目前在 macOS 27 验证 |
| 后台帧率 | 失焦后限制到 15 FPS，回到前台恢复原帧率 |
| 光影对比 | F11 或 ⌘⇧G 临时恢复默认阴影，再按恢复原来的增强设置 |

首版使用玩家已安装的客户端资源。角色模型、高清贴图、MOD、模型提取工具和官方着色器文件均不在本仓库。

当前实验版的额外 SSAO、接触阴影、SSR、角色 PC 高光/材质着色器还依赖客户端生成资源，留待独立的本地生成流程完善后再接入。这里的“角色自阴影”与上述屏幕空间效果是不同功能。

## 使用打包版

在 PlayCover 安装对应服的客户端，先正常启动并下载完游戏资料，然后退出游戏。解压 `BetterMacField-macOS-arm64.zip`，打开 `BetterMacField.app`，选择国际服或国服。加载约 20 秒后顶部出现 **画面** 菜单。

打包版包含启动器和本项目动态库；玩家仍需自己的 IPA、PlayCover 和游戏资料。源码构建者运行 `python3 build_release.py` 可生成同样的包。当前包采用本地签名，尚未经过 Apple 公证，首次下载打开可能被 macOS 拦截，见 [使用与录制](docs/quickstart.md)。

## 从源码构建与启动

需要 Apple Silicon Mac、macOS 13 或更新版本、Xcode Command Line Tools、原生 arm64 Python 3.9+，以及已通过 PlayCover 安装并能正常启动的游戏。完整 Metal HUD 菜单取决于系统版本，其余功能也仍需其他系统版本的实际验证。

```sh
git clone https://github.com/Shalistera/BetterMacField.git
cd BetterMacField
python3 bettermacfield.py check
python3 bettermacfield.py launch
# 国服
python3 bettermacfield.py --region cn launch
```

游戏已经运行时：

```sh
python3 bettermacfield.py launch --restart
```

安装位置不同时，把 `--app` 放在子命令前：

```sh
python3 bettermacfield.py --app '/path/to/com.gryphline.endfield.ios.app' launch
```

启动器先检查 UnityFramework 与 IL2CPP metadata 的 SHA-256，编译并临时注入本项目的动态库。加载需要约 20 秒；登录后进入场景，即可使用顶部 **画面** 菜单。PlayTools 继续保留，PlayCover 的触控按键映射关闭，由本项目处理场景键鼠。登录 SDK、内置网页及原生控件继续使用正常鼠标点击。

首次使用默认沿用游戏 TAAU。进入场景后可选择：

**画面 → 画质提升 → MetalFX Temporal＋原生环境光**

分辨率、超分档位和角色阴影设置会保存；更新客户端后，版本和二进制校验不匹配会拒绝启动增强版。游戏本体、资源包和 PlayTools 文件保持原状。窗口最大尺寸受当前桌面可用空间限制，输出分辨率与窗口点数分别处理。

录制对比时按 **F11**（部分键盘需 Fn+F11），也可按 **⌘⇧G** 或点击“画面 → 光影对比”。再次按键恢复之前的角色自阴影和阴影精度。对比保留输出分辨率、MetalFX、LOD 和资源状态，用来展示阴影增强；完整原版对照请退出后从 PlayCover 启动。临时对比不会保存为下一次启动默认。

## 恢复与排错

直接关闭游戏，再从 PlayCover 正常启动，即可运行不加载本项目动态库的版本。

本工具会将 PlayCover 的 UI 缩放设为 2×，采用菜单控制的窗口比例，并关闭触控按键映射。这几项原设置只保存一份。关闭游戏后恢复：

打包版：重新打开启动器，勾选 **恢复 PlayCover 设置**，再选择对应服。

源码版：

```sh
python3 bettermacfield.py restore-settings
python3 bettermacfield.py --region cn restore-settings
```

诊断命令只读取当前增强进程：

```sh
python3 bettermacfield.py status
```

运行日志位于 `~/Library/Containers/<客户端 ID>/Data/tmp/BetterMacField/bettermacfield-runtime.log`。国际服 ID 为 `com.gryphline.endfield.ios`，国服为 `com.hypergryph.endfield`。两服运行文件和设置恢复记录分别保存。报告问题时附客户端版本、macOS 版本、芯片、所选画质模式与相关日志。请自行去除账号等个人信息。

LOD 改回手机档会恢复参数，已经驻留的高精度内容由游戏继续管理，因此画面不一定立即恢复为完全相同的低 LOD 截图。Spatial 的移动拖影已知存在；优先使用 Temporal 或 TAAU。

## 开发

```sh
python3 bettermacfield.py build
python3 -m unittest discover -s tests
```

实现边界、更新适配点见 [架构与适配](docs/architecture.md)。当前发布范围与实际验证见 [验证记录](docs/validation.md)。

源码采用 [MIT](LICENSE)；游戏内容、商标和第三方组件的权利仍归各自权利人，参见 [NOTICE](NOTICE)。
