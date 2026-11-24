# Sakura Editor Word Marker 自定义增强

本增强基于 Sakura Editor 官方源码 Commit `fe1a4a281c6e6bec2a1a1bc39059fe7424bb5075`，保留原项目 Zlib 许可证。About 对话框会显示 `Custom enhancement build: Word Marker`。

## 操作

- 双击或拖选单行文字：在当前文档中自动标记所有完全相同的文字。自动标记按文档分别保存于当前进程内存。
- Ctrl＋双击：将选中的文字加入固定 Marker；对同一文字再次执行会移除。
- 右键菜单的「マーカー」：为选中文字添加/移除固定 Marker；现有固定 Marker 子菜单支持单项解除、颜色轮换和全部解除。
- 共通設定「全般」页的「選択語を自動マーカー表示」控制自动 Marker。
- 新建配置的默认工具栏包含「右クリックメニュー」按钮；已有自定义工具栏不会自动更改，可在共通設定「ツールバー」页自行添加。

固定 Marker 上限为 32 项，颜色从当前文档类型的搜索色板中分配。固定 Marker 不接受跨行文本。Marker 不改变文本、搜索条件或搜索历史；固定项及自动 Marker 开关保存在 `sakura.ini` 的 `[WordMarker]` 节。自动选中文字本身不写入 INI。

## 构建与验证状态

GitHub Actions 的 Win32 Release 作业会额外上传独立 `sakura.exe`。将源码推送到有权限的 GitHub fork 后即可运行项目工作流。

当前工作区尚未完成 Windows 编译和运行验证。单元测试、大文件扫描测试、UI 回归及 `sakura.exe` 产物状态见 [WORD_MARKER_TEST_REPORT.md](WORD_MARKER_TEST_REPORT.md)。
