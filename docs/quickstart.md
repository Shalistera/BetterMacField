# BetterMacField 使用与录制

适用范围：Apple Silicon Mac，已经在 PlayCover 安装并下载完资料的国际服或国服 iOS 1.6.3。启动器会核对完整二进制；同一个版本号的其他构建也可能需要重新适配。

## 日常启动

1. 退出游戏，解压发布包，将 BetterMacField.app 放到方便的位置。
2. 双击启动器，选择国际服或国服。
3. 正常登录。约 20 秒后，顶部出现“画面”菜单。
4. 场景中按 WASD 或使用鼠标后会自动切换键鼠 HUD。按住 Alt/Option 释放鼠标；登录 SDK 和内置页面保留点击操作。

默认使用游戏 TAAU。需要 MetalFX 时，选“画面 → 画质提升 → MetalFX Temporal＋原生环境光”。输出分辨率、超分档位和角色自阴影均在同一个菜单中。Spatial 保留为备选，当前存在移动拖影。

如果提示游戏已运行，选择“退出并重新启动”即可加载增强。更新客户端后若提示身份不匹配，请使用 PlayCover 原版入口，等待对应适配。

## 拍摄前后对比

先选择角色自阴影和所需阴影精度，保持镜头、角色与位置固定。按 F11 临时恢复原生阴影，再按恢复刚才的增强设置。部分键盘需 Fn+F11；也可以使用 ⌘⇧G 或“画面 → 光影对比”。

这项对比控制角色自阴影和阴影精度。输出分辨率、超分、LOD 和已加载资源保持当前状态。录制时可标注“原生阴影 / 增强阴影”；完整原版对照需要退出增强进程后从 PlayCover 启动。暂时对比状态不会成为下次启动默认。

公开包使用游戏原始资源。开发版的高清模型、贴图替换、MOD、额外 GTAO/SSAO、接触阴影和 SSR 不属于此包，不应使用包含这些功能的开发版视频代表公开包全部效果。

## 回到 PlayCover 原设置

退出游戏，打开 BetterMacField，勾选“恢复 PlayCover 设置”，再选择对应服。随后从 PlayCover 启动。每个服只保留一份原设置记录，并保留用户在此后单独修改过的字段。

## 第一次下载后的 macOS 提示

当前包采用本地签名，尚未通过 Apple 开发者公证。若提示无法验证开发者或 Apple 无法检查软件，确认下载来源后，可按 [Apple 的说明](https://support.apple.com/zh-cn/102445) 在“系统设置 → 隐私与安全性”使用“仍要打开”。具体可用入口以系统提示为准。

“已损坏”或“将损坏电脑”是另一类提示，应保留原提示并检查包的来源和完整性。发布包同时提供 SHA256SUMS.txt；本机启动验证不能代替另一台 Mac 的下载与首次打开验证。

## 反馈问题

记录客户端区域和版本、macOS、芯片、画质选项以及发生位置。日志位于：

- 国际服：~/Library/Containers/com.gryphline.endfield.ios/Data/tmp/BetterMacField/bettermacfield-runtime.log
- 国服：~/Library/Containers/com.hypergryph.endfield/Data/tmp/BetterMacField/bettermacfield-runtime.log

发送日志前清除账号信息。失焦时后台上限为 15 FPS，性能对比应保持游戏窗口在前台。
