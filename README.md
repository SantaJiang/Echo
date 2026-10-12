# Echo

纯本地、跨平台的高品质音乐播放器。界面按 Apple Music 的资料库布局，只包含歌曲、专辑、艺人、文件夹和歌单。

## 构建

依赖：Qt 6.5+（Widgets、Sql）、FFmpeg、TagLib、CMake 3.24+、C++20。Linux 额外需要 PipeWire 或 ALSA，以及 Qt DBus。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase)"
cmake --build build -j
./build/echo-selftest
QT_QPA_PLATFORM=offscreen ./build/echo-uitest
./build/Echo.app/Contents/MacOS/Echo
```

Linux 与 Windows 的可执行文件名也是 `Echo`。安装包通过 CPack 生成：

```bash
cmake --build build --target package
```

macOS 生成 dmg，Windows 生成 NSIS 安装程序，Linux 生成 deb。
