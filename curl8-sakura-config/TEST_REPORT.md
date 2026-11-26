# Curl 8.0 Sakura 配置包测试报告

- 基准代码：`fe1a4a281c6e6bec2a1a1bc39059fe7424bb5075`
- 目标：Sakura Editor 2.4.3.7173 GHA Win32
- 制作日期：2026-11-25
- Sakura 源码改动：无（本交付仅新增配置文件）

## 静态检查

- 类型配置头：` タイプ別設定 Ver1`；包含 `[Info]`、`[Types]`、`[TypeEx]`，版本结构号 182 与该提交 `N_SHAREDATA_VERSION` 一致。
- 类型导入器字段：``nInts` 使用源码定义的 12 个整数槽位，首槽为非零值，避免被导入器判作“基本类型”并重置名称/扩展名；按 `CImpExpManager.cpp`、`CShareData_IO.cpp` 的 `[Info]`/`[Types]`/`[TypeEx]`/`RxKey[nnn]`/`C[NAME]`/`szKeyword[n]`/`szKeywordFile[n]` 模型编制；十个关键字集合均有随包 `.kwd`。
- 数量：10 个关键字类别，615 个分类词条（609 个不同词），609 个补完词，10 条正则（低于原生上限 100；最长规则小于 1000 字符）。
- 扩展名：`curl,dcurl,mcurl,scurl,xcurl,pcurl`；不注册 Windows Shell 默认关联。
- 配色：按用户给出的前景 RGB HEX 值制作；背景统一为 Sakura 2.4.3 源码默认 `RGB(255,251,240)`（`#FFFBF0`），不再使用纯白。导入器底层 `COLORREF` 文件值采用 BGR 表示，已按通道转换。
- 示例：包含 `||` 行注释、`|#...#|` 块注释、带标签注释、字符串转义、整数/浮点、横杠标识符、流程控制和 GUI 控件。

## 未完成的实机验证

当前环境是 macOS，未运行目标 Windows Sakura 2.4.3.7173 Win32 GUI，因此没有声称已经完成实际导入渲染、补完弹窗、颜色效果或 Windows关联检查。该配置采用官方导入结构与目标提交字段，但最终仍需在目标版本 Sakura 的新类型中实际导入确认。

本环境未运行 Sakura 的 BREGEXP DLL 正则编译器；正则表达式内容经生成和格式/数量静态检查，但 lookbehind/lookahead 是否被用户安装包里的具体 Bregonig/Bregexp 版本接受，仍需在 Windows 导入并观察正则关键字设置页确认。若该版本不接受边界断言，可删除 `.rkw` 中的横杠单词规则，普通关键字仍可用。

## 已知能力边界

- 原生注释字段覆盖 `||` 和 `|#...#|`；带标签注释由单行正则辅助着色并验证开始/结束标签一致，但不具备跨行状态。
- Regex keyword 是行级色彩策略，不会解析 Curl 的完整语法结构；字符串内部的规则屏蔽、嵌套注释、长度指定注释及所有 verbatim 字符串变体无法完全模拟。
- 词库以 RTE 8.0 官方指南/API 高频核心名为基线，未宣称穷尽完整 API。
