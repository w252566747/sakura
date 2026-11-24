# Sakura Word Marker 增强版测试记录

## 基线

- 源码基线：`fe1a4a281c6e6bec2a1a1bc39059fe7424bb5075`（工作区 HEAD 已核实）。
- 定位：基于 Sakura Editor 的自定义增强，原项目 Zlib 许可证及版权声明保留。
- 新设置仅使用 `sakura.ini` 的 `[WordMarker]` 节，不改动共享内存结构或既有搜索状态。

## 当前验证

| 检查 | 结果 |
| --- | --- |
| `git diff --check` | 通过 |
| `sakura_core/sakura.vcxproj` XML | 通过 |
| `sakura_core/tests1.vcxproj` XML | 通过 |
| 资源文件编码和控件 ID | 通过静态核对：RC 保持 UTF-16LE BOM/CRLF，资源头保持 UTF-8 BOM/CRLF |
| Marker 单元测试 | Win32 Debug 与 x64 Debug 均通过 6 项：固定项匹配/切换、数量上限、颜色槽分配、最长重叠匹配、自动匹配和大行扫描；Win32 Debug 的 1 MiB 行扫描测试用时 454 ms |
| 本机 Matcher 冒烟测试 | 通过：直接编译 `CWordMarker.cpp`（含 `-Wall -Wextra -Werror`），覆盖固定/自动匹配、文档隔离、最长重叠和 1 MiB 行扫描；本次 `-O2` 约 7 ms、无优化约 57 ms（当前 Mac 算法冒烟值，不代表 Win32 UI 性能） |
| 仅替换 `sakura.exe` | 静态上可行：未增加 DLL、插件、外部运行库或共享内存字段；必须在原有 Sakura 安装依赖目录中验证后才能确认 |
| Windows Release 编译 | GitHub Actions 成功：Release Win32、Release x64；Debug Win32、Debug x64 也成功 |
| 单元测试执行 | GitHub Actions 成功：Win32 Debug 的 1226 项 C++ 测试和 pytest 均通过；x64 Debug 的 1227 项 C++ 测试和 pytest 均通过；Test Results 作业成功 |
| 大行性能测试 | Win32 Debug 的 Marker 1 MiB 行扫描单测通过，耗时 454 ms；此前本机独立 Matcher 冒烟 `-O2` 约 7 ms。尚未完成真实界面打开超大文档后的滚动/重绘性能测量 |
| PE 架构核对 | `dist/sakura.exe` 为 PE32 Intel 80386（Win32） |
| EXE 产物 | 已生成：`dist/sakura.exe`，2,278,400 字节；SHA-256 `1deedf3cb4cef190856b918bf298e701cf37854bd498a5be92201945b0d94305` |
| Artifact 归档 | `dist/SakuraEditor-WordMarker-Win32-Release-EXE.zip`；SHA-256 `f4288b9bda195781ccd5a32e7715a9487c047d42010d319519862fd8a8f22cbc`，与 Actions artifact digest 相同 |
| CI 运行 | [GitHub Actions run #6](https://github.com/w252566747/sakura/actions/runs/45401638417)，全部 11 个作业成功；源码提交 `4e39364e97be1fe6406a446d92ddaaec71498736` |
| 真实 GUI 回归 | 尚未在 Windows 桌面上手工验证双击/拖选/右键/设置页，以及“仅替换 sakura.exe”后的行为；CI 编译和自动测试不能代替此项 |

## 本轮工具栏与颜色改动（2026-11-24）

- 已在本地源码加入：自动/固定 Marker 跳过黄色检索槽、旧 INI 中存储的黄色 Marker 显示时回退到非黄色槽、工具栏右侧显示词条并按对应 Marker 前景/底色绘制、单项关闭/全部清除、空间不足时以 `+N` 菜单访问其余词条；右键换色及鼠标创建/移除后即时刷新工具栏。
- `git diff --check` 通过。独立编译 `CWordMarker.cpp` 的本机 C++ 冒烟测试通过：颜色槽 0 不会作为新 Marker 颜色；固定/自动匹配、文档隔离、最长匹配、清除及 Getter 均通过；1 MiB 单行扫描约 7 ms（本机优化构建，仅供算法级参考）。
- 当前环境没有 MSVC、Windows SDK 或 MinGW，无法本机编译 Win32 UI。尝试把新提交写入 GitHub 增强分支时被自动审查终止；核对后远端分支仍为 `4e39364e97be1fe6406a446d92ddaaec71498736`，本轮 Windows Actions 尚未运行。
- 因此，`dist/sakura.exe` 和上表中成功的 Actions 运行均是前一轮代码产物，不含本轮黄色修正和工具栏词条控件。不要用该 exe 验收本轮改动；待本轮源码进入 Windows Actions 并成功构建后再更新 exe 和哈希。

Marker 状态没有加入 `DLLSHAREDATA`、`CommonSetting` 或共享内存版本结构；只新增 `CPropCommon` 的进程内对话框暂存字段及 `sakura.ini` 的 `[WordMarker]` 项。

## 验证边界与后续手工检查

自动/固定 Marker、共通設定开关、日语右键管理菜单以及默认工具栏管理入口已编译。Marker 使用现有可视区域绘制流程；没有独立的跨重绘命中缓存。共享内存结构未加入 Marker 字段，固定项和开关保存在 INI 的 `[WordMarker]` 节。默认工具栏仅对新配置生效，既有自定义工具栏不会自动迁移。

只替换 EXE 在构建依赖层面可行：没有新增 DLL、插件或运行库，新增状态不在共享内存中。但尚未在目标 Windows 安装目录实机替换启动验证。建议先备份原 `sakura.exe`，在 Sakura 已关闭时替换，再启动并确认旧版 INI、宏、编码和搜索功能正常；若需要多进程兼容验证，也需在 Windows 上测试新旧版本同时运行。
