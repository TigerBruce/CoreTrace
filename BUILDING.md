# 在另一台 Windows 电脑修改 CoreTrace

## 获取与编译

1. 克隆此仓库，或从 GitHub 下载源码 ZIP 并解压。
2. 在项目目录用 PowerShell 执行：

   ```powershell
   powershell -NoProfile -File .\setup-tools.ps1
   ```

   该脚本从 llvm-mingw 官方 GitHub Release 下载固定版本 20261020，并核对 SHA-256。编译器下载约 182 MiB，解压后约 718 MiB；它仅用于构建，不随程序启动。若本机脚本策略禁止运行，可手动下载同一压缩包，核对脚本中的哈希，解压到 tools 目录。
3. 关闭正在运行的 CoreTrace，再双击 `build.cmd`。构建完成后运行 `native-portable/CoreTrace.exe`。

需要 Windows 10/11 x64；使用 C++20、Win32/GDI、PDH。不需要 .NET、Node.js、Python、Visual Studio 或网络服务。首次准备编译器需要网络，后续构建可离线完成。构建脚本也支持通过 `CORETRACE_TOOLCHAIN` 指定 llvm-mingw 的 bin 目录。

## 代码入口

- `native-src/core.hpp`：配置、CPU 拓扑及采样数据结构。
- `native-src/telemetry.cpp`：Windows CPU 计数器、进程内存和 PawnIO 传感器读取。
- `native-src/ui.cpp`：原生窗口、GDI 图表、设置、透明度和托盘菜单。
- `native-src/memory-speed.hpp`：内存配置速率读取。
- `native-src/CoreTrace.rc`、manifest、ICO、PNG：版本资源、窗口配置及原始图标素材。

默认 Utility 模式匹配开发机 Windows 任务管理器的口径；忙碌时间模式保留 Processor Time。每核图取逻辑线程最大值，总体 Utility 直接使用 Windows _Total 计数器。两种指标均存于有限的内存历史，切换不混合口径。图表含 100% 参考线。

## 硬件监控依赖

CPU 占用和内存统计不需要 PawnIO。每核温度、硬件频率需要另行安装官方 [PawnIO](https://github.com/namazso/PawnIO) 驱动。应用不会自动安装驱动。当前实现仅为代码中列出的 Intel Alder/Raptor Lake 型号读取 MSR，其他型号的温度和频率可能不可用。

`native-portable/IntelMSR.bin` 是必需的第三方签名模块，不是临时编译垃圾；保持它与 EXE 同目录。模块来源、上游源码和许可证见 `native-portable/THIRD-PARTY.txt`。`LICENSE-MPL-2.0.txt` 等文件为依赖许可材料，不代表给整个项目额外选择了开源许可证。

## 仓库范围

提交完整的当前应用源码、构建脚本、图标原素材、必要运行模块与许可证。通过 .gitignore 排除编译器、驱动安装包、生成的 EXE/RES、个人 settings.ini、历史截图和诊断日志。运行目录中的其他文件已经保留，编译后可以直接打包复制。
