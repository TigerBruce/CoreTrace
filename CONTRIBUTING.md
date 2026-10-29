# Contributing to CoreTrace

中文或 English 均可。欢迎问题反馈、硬件兼容性验证、文档修改和代码贡献。

## 报告问题

先搜索 [已有 Issues](https://github.com/TigerBruce/CoreTrace/issues)。请提供：

- CoreTrace 提交号或构建产物来源、Windows 版本、CPU 型号。
- 最短复现步骤、预期结果和实际结果。
- 显示问题：主题、字号、缩放比例、窗口尺寸，以及固定/编辑模式。
- 统计问题：Utility 或 Time、比较工具与视图、采样时段；不要只比较两个不同时间的瞬时数字。
- 温度/频率问题：PawnIO 是否安装、模块是否在 EXE 同目录。

截图请只保留相关区域。不要提交访问令牌、个人 settings.ini、完整桌面或无关的进程/工程数据。

## 提交修改

1. Fork 仓库，从 main 建立一个描述清楚的分支。
2. 按 [BUILDING.md](BUILDING.md) 构建。使用 `native-src/.clang-format` 中的风格，只格式化涉及的代码。
3. 一个 PR 解决一个明确问题，解释触发场景、改后行为和验证结果。
4. 若修改指标、操作方式或硬件范围，同时更新相应说明。界面修改附深浅色预览。

提交贡献意味着你有权提交这些内容，并同意原创贡献使用项目 MIT 许可证。第三方代码或资源需要保留来源与许可说明。

## 设计约束

- 优先低 CPU、低内存和可理解的统计口径；避免为视觉效果引入高频刷新或大型运行时。
- 历史和缓存应有界；不要每轮采样重建不必要的系统对象或启动外部进程。
- 保持数据采样与 UI 响应分开，避免在绘制回调中做慢速硬件查询。
- 不可用的传感器或计数器应保持缺失状态，不能伪装成 0 或悄悄替换成另一种指标。
- 不自动安装驱动，不增加默认遥测上传，不把用户配置写进发布包。

## 按改动验证

纯文档修改检查链接和事实即可。代码修改至少完成 Windows x64 构建，并覆盖实际影响的场景：

| 修改范围 | 建议验证 |
| --- | --- |
| CPU 采样 | 两种模式、线程与物理核心映射、缺失值、短时独立计数器对照 |
| 绘图或布局 | 深浅主题、窄宽窗口、较大字号、固定/编辑反复切换 |
| 透明度 | 外框/卡片独立变化，文字曲线清晰，无桌面残影 |
| 生命周期 | 单实例、退出、设置保存、GDI/线程资源释放 |
| 性能优化 | 同硬件、同窗口/透明设置下比较 CPU、工作集和私有提交；注明时长 |

CI 执行构建和便携打包，不等同于物理硬件传感器或交互验收。不要将短时采样称为长期稳定性测试。若没有所需硬件，在 PR 中明确说明。

## English summary

Open an issue with a reproducible scenario, commit/build source, Windows version, CPU model and relevant settings. For counter discrepancies, specify Utility/Time and the comparison view. Build with the documented llvm-mingw toolchain; keep pull requests focused and explain validation. Preserve low-overhead sampling, bounded history, missing-value semantics and portable settings. Do not add generated binaries, local settings or unrelated diagnostics to source commits. Original contributions are accepted under MIT; identify third-party material separately.
