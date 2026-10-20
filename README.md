# Echo

纯本地、跨平台的高品质音乐播放器。界面按 Apple Music 的资料库布局，只包含歌曲、专辑、艺人、文件夹和歌单。

## 构建

依赖：Qt 6.5+（Widgets、Sql）、FFmpeg、TagLib、libebur128、CMake 3.24+、C++20。Linux 额外需要 PipeWire 或 ALSA，以及 Qt DBus。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase)"
cmake --build build -j
./build/echo-selftest
./build/echo-playbacktest
QT_QPA_PLATFORM=offscreen ./build/echo-uitest
./build/Echo.app/Contents/MacOS/Echo
```

Linux 与 Windows 的可执行文件名也是 `Echo`。安装包通过 CPack 生成：

```bash
cmake --build build --target package
```

macOS 生成 dmg，Windows 生成 NSIS 安装程序，Linux 生成 deb。

## 播放与窗口

- 关闭主窗口会隐藏到菜单栏 / 系统托盘，音乐继续播放；点击常驻图标打开主窗口，右键可退出。也可通过菜单或 Cmd/Ctrl+Q 退出。
- 从侧栏或 Cmd/Ctrl+, 打开设置，开启「音量均衡」。后台按 EBU R128 分析当前播放队列（包含没有 ReplayGain 标签的歌曲），目标为 -18 LUFS，并保留 1 dB 真峰值余量。峰值限制可能使个别歌曲低于目标响度。
- 首次分析完成前按原音量播放；结果按文件大小、修改时间和 CUE 范围缓存，文件变化后重新分析，不修改音乐文件。开启均衡后优先使用测量结果；关闭后恢复原有 ReplayGain 设置。
- 迷你播放器无标题栏、始终置顶，可拖动封面、文字和空白区域；右侧展开按钮返回主窗口。

## 我喜欢的

- 播放列表顶部固定显示「我喜欢的」，不能删除、改名或移动。歌曲按最近喜欢在前排列，取消后重新喜欢会回到顶部。
- 在任意歌曲列表的歌名左侧点爱心，或通过主播放栏、迷你播放器的爱心收藏当前歌曲；暂停时也可操作。未喜欢的爱心在悬停该行时显示，已喜欢的爱心始终显示。
- 再次点心或在收藏列表中选择「取消喜欢」即可移除收藏，不会删除音乐或中断播放；支持撤销、重做，重启后保留收藏。
- 支持导出收藏列表；查看收藏列表时导入 M3U 会创建普通歌单。

## AirPods 空间音频（macOS）

在设置中开启「AirPods 空间音频」，连接支持空间音频的 AirPods 并播放歌曲，然后在 macOS 控制中心的声音面板选择 Echo 的「固定」或「头部跟踪」。系统最终决定当前设备是否可用及是否启用效果，Echo 不会把“允许空间化”误报为头部跟踪已经生效。

开启后采用 Apple 的 `AVSampleBufferAudioRenderer`，允许单声道、立体声和多声道内容由系统空间化；解码、音量均衡、进度控制和无缝切歌仍由 Echo 处理。不需要访问头部姿态的额外权限。普通立体声的空间化不代表音源具有 Dolby Atmos 元数据。双耳录音建议关闭此开关，避免二次空间化。

空间音频与独占输出互斥，切换时保留播放位置和暂停状态。系统输出失败时回退到 CoreAudio，并在设置中显示原因。默认关闭，选择会保存；系统输出会跟随默认设备，包括 AirPods 的自动切换。

在连接实际输出设备的 Mac 上可运行静音原生后端测试（不播放测试音）：

```bash
./build/echo-spatialtest
```

接口依据：[Apple WWDC23](https://developer.apple.com/videos/play/wwdc2023/10233/) 与本机 SDK 的 `allowedAudioSpatializationFormats` 说明。
