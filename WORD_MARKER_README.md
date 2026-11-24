# Sakura Editor Word Marker 自定义增强

本增强基于 Sakura Editor 官方源码 Commit `fe1a4a281c6e6bec2a1a1bc39059fe7424bb5075`，保留原项目 Zlib 许可证。About 对话框会显示 `Custom enhancement build: Word Marker`。

## 操作

- 双击或拖选单行文字：在当前文档中自动标记所有完全相同的文字。自动标记按文档分别保存于当前进程内存。
- Ctrl＋双击：将选中的文字加入固定 Marker；对同一文字再次执行会移除。
- 右键菜单的「マーカー」：为选中文字添加/移除固定 Marker；现有固定 Marker 子菜单支持单项解除、颜色轮换和全部解除。
- 共通設定「全般」页的「選択語を自動マーカー表示」控制自动 Marker。
- 新建配置的默认工具栏包含「右クリックメニュー」按钮；已有自定义工具栏不会自动更改，可在共通設定「ツールバー」页自行添加。

固定 Marker 上限为 32 项，颜色从当前文档类型的搜索色板中分配，但跳过与检索重复的黄色槽。固定 Marker 不接受跨行文本。Marker 不改变文本、搜索条件或搜索历史；固定项及自动 Marker 开关保存在 `sakura.ini` 的 `[WordMarker]` 节。自动选中文字本身不写入 INI。

工具栏右侧显示当前固定 Marker 和当前文档的自动 Marker；词条按钮的底色/文字色跟随文档类型的 Marker 配色，点击词条上的 × 可单独移除，“全解除”清除当前显示的自动 Marker 和全部固定 Marker。空间不足时以 `+N` 菜单列出其余固定词条供单独删除。右键菜单换色后工具栏即时刷新。

## 构建与验证状态

此前的 Win32 Release 已由 GitHub Actions 编译，位于 `dist/sakura.exe`，但它**不包含**本轮新增的右侧 Marker 词条按钮、随色刷新和排除黄色改动。当前这轮源码尚未完成 Windows 编译，因此该 EXE 不能作为本轮功能的交付版本。此前 CI 的测试结果见测试报告；本轮本机独立 Matcher 冒烟通过（包含排除颜色槽 0 和 1 MiB 行扫描，约 7 ms）。

尚未完成真实 Windows 桌面 GUI 回归和仅替换 `sakura.exe` 的实机启动验证；大文件滚动/重绘也尚无真实 UI 性能测量。完整测试结果、哈希和验证边界见 [WORD_MARKER_TEST_REPORT.md](WORD_MARKER_TEST_REPORT.md)。
