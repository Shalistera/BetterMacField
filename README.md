# BetterMacField

为通过 PlayCover 运行的《明日方舟：终末地》iOS 客户端补充 macOS 键鼠、光影与画面设置。当前适配国际服、国服 **iOS 1.6.3 的指定构建**。

## Launcher 功能

| 页面 | 功能 |
| --- | --- |
| 画面 | 720p / 1080p / 1440p / 2160p；TAAU、MetalFX Temporal / Spatial；质量 66.7%、平衡 58.8%、性能 50%；锐化；手机 / PC 中档 / PC 高档 LOD |
| 光影 | 角色自阴影、角色光照与滤波、阴影精度、原版 GTAO、接触阴影、SSR、可选实验性 PC 高光 |
| 操作 | 自动键鼠、Shift/E 与鼠标滚轮桥接、工具转盘、SDK/浏览器点击兼容、后台 15 FPS 节能、系统 Metal HUD |
| 状态与恢复 | 客户端和组件校验、运行状态、日志复制、PlayCover 设置恢复 |

**F11 / ⌘⇧G / Launcher 按钮**临时切换原生与增强光影，再按恢复原设置；输出分辨率、超分、LOD 保持一致。此对比包含 GTAO、接触阴影、SSR、角色光照、自阴影和阴影精度。

Launcher 与游戏内菜单共用设置项，两服分别保存。玩家使用 `.app` 无需 Python、Xcode 或终端。客户端二进制与资源包保持原状，正常退出后从 PlayCover 启动即可运行原版。

本包排除 MOD、模型/贴图替换与提取功能。原版环境光继续由游戏负责，尚未完成的额外 GI 实验未作为功能发布。

## 使用

在 PlayCover 安装对应服的 IPA，正常启动并下载完资料，然后退出。解压 `BetterMacField-macOS-arm64.zip`，打开 `BetterMacField.app`，选择区域与设置，再点击「启动游戏」。进入场景后设置自动应用，也可以实时保存调整。

默认 1080p、质量档、TAAU，开启角色光影、GTAO和接触阴影。SSR、实验性 PC 高光可自行开启。完整步骤、录制对比和恢复说明见 [使用说明](docs/quickstart.md)。

当前使用本地签名，尚未 Apple 公证；其他 Mac 下载后的首次打开仍需实际验证。完整 Metal HUD 菜单在 macOS 27 验证，其可用项取决于系统版本。

## 源码与渲染程序的边界

MIT 许可证适用于本项目的 Launcher、输入桥接与图形集成代码。游戏原始着色器及据其适配的渲染程序仍属于原权利人，详见 [NOTICE](NOTICE)。模型、纹理、游戏 bundle、IPA 和 MOD 数据均不在源码仓库中。

完整光影构建还需要本地导出的渲染程序集合。该集合与 MIT 源码分开存放于被 Git 忽略的 `build/Graphics`，包括原版 GTAO/接触阴影计算程序、适配的光照程序及绑定信息。当前构建工具从已验证的开发导出目录准备它；**尚未提供从朋友当前客户端自动提取该集合的独立流程**。本地完整 Launcher 已把所需程序打包，运行不依赖开发目录。不要把仓库可直接构建动态库等同于完整光影包可脱离该输入重建。

## 开发构建

需要 Apple Silicon、macOS 13+、Xcode Command Line Tools 和 Python 3.9+。当前验证集中在 macOS 27；其他系统尚需实测。

```sh
python3 bettermacfield.py build
python3 -m unittest discover -s tests
# 使用已验证的本地渲染程序导出：
python3 prepare_graphics.py /path/to/local-rendering-export
python3 build_release.py
# 两服使用相互隔离的运行目录：
python3 bettermacfield.py launch
python3 bettermacfield.py --region cn launch
```

`dist` 始终保留一份当前 `.app`、ZIP 与 SHA256SUMS.txt。构建失败时不应交付旧 ZIP 代替本次结果。启动器精确校验 UnityFramework、IL2CPP metadata 与打包组件；客户端更新后拒绝加载不匹配的适配。

## 已知情况

macOS 27 上的完整开发版曾发生 Temporal / ANE 断言退出，目前尚未确定触发条件，因此默认 TAAU。Spatial 有移动拖影。LOD 恢复后驻留内容由游戏释放。地图与角色覆盖范围、实际验证和已知问题见 [验证记录](docs/validation.md)，实现与更新适配见 [架构](docs/architecture.md)。
