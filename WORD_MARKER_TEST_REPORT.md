# Sakura Word Marker 增强版测试记录

## 基线

- 源码基线：`fe1a4a281c6e6bec2a1a1bc39059fe7424bb5075`（工作区 HEAD 已核实）。
- 定位：基于 Sakura Editor 的自定义增强，原项目 Zlib 许可证及版权声明保留。
- 新设置仅使用 `sakura.ini` 的 `[WordMarker]` 节，不改动共享内存结构或既有搜索状态。

## 追加单词边界功能（2026-11-25）

- 共通設定「全般」增加 `単語として扱う追加文字`，默认 `_-`；「原版に戻す」将配置置空，双击选择回到 Sakura 原有规则。
- 只在鼠标单词选择路径扩展连续的原有 ASCII 单词字符和追加字符；键盘选择命令、正则搜索、SQL 解析仍走原规则。
- 自动及固定 Marker 共用相同字符边界判断；配置读写使用 `[WordMarker] ExtraWordChars`，兼容旧 INI 缺少此键的情况。
- 本轮未执行 Windows 编译或 GUI/回归测试。静态 `git diff --check`、项目 XML 解析、RC/资源头 BOM 与 CRLF 检查通过。

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
- 本机没有 MSVC/Windows SDK/MinGW。源码已推送至 GitHub 增强分支，并由 Windows Actions 完成构建（最终结果见下节）。
- 本地 `dist/sakura.exe`（SHA-256 `1deedf3c…`）和其 ZIP（`f4288b9b…`）是旧构建产物，不包含最终修正；请从下节列出的最新 Actions 运行下载独立 EXE artifact。

Marker 状态没有加入 `DLLSHAREDATA`、`CommonSetting` 或共享内存版本结构；只新增 `CPropCommon` 的进程内对话框暂存字段及 `sakura.ini` 的 `[WordMarker]` 项。

## 验证边界与后续手工检查

自动/固定 Marker、共通設定开关、日语右键管理菜单以及默认工具栏管理入口已编译。Marker 使用现有可视区域绘制流程；没有独立的跨重绘命中缓存。共享内存结构未加入 Marker 字段，固定项和开关保存在 INI 的 `[WordMarker]` 节。默认工具栏仅对新配置生效，既有自定义工具栏不会自动迁移。

只替换 EXE 在构建依赖层面可行：没有新增 DLL、插件或运行库，新增状态不在共享内存中。但尚未在目标 Windows 安装目录实机替换启动验证。建议先备份原 `sakura.exe`，在 Sakura 已关闭时替换，再启动并确认旧版 INI、宏、编码和搜索功能正常；若需要多进程兼容验证，也需在 Windows 上测试新旧版本同时运行。

## 修复后最终验证（2026-11-25）

- 最终源码提交：`dc7d649296a6c053d4bcd016238798408cb613e7`，基于指定基线并包含工具栏编译修正。
- Windows Actions [build sakura run #8](https://github.com/w252566747/sakura/actions/runs/45503316560) 全部 11 个作业成功：Win32/x64 Debug 与 Release 四种配置均编译通过；四个配置的 C++/Python 测试均通过。Win32 Release 为 1170 项 C++ 测试、66 项 pytest；Win32 Debug 为 1226 项 C++ 测试、66 项 pytest；x64 Debug 为 1227 项 C++ 测试、66 项 pytest。x64 Release 作业成功。
- 最新 Win32 Release 独立可执行文件 artifact：`SakuraEditor-WordMarker-Win32-Release-EXE`，ZIP 大小 1,170,998 字节，Actions artifact SHA-256 `22cabd16c18cf72973e70515a4e7066fa6b78637fe770067ec1d171cfe030e61`。从 run #8 的 Artifacts 区下载该 ZIP，解压即可得到 `sakura.exe`；构件由 Win32 Release 步骤直接上传。
- Doxygen 检查通过：[Doxygen run #8](https://github.com/w252566747/sakura/actions/runs/45503316517)。
- MinGW Build/Test 与 Cppcheck 的独立 workflow 在 MSYS2 软件包安装步骤失败，未运行到源码编译/静态分析：当前请求的 `mingw-w64-x86_64-diffutils` 和 `mingw-w64-x86_64-cppcheck` 包名不可用。它们不影响 MSVC Win32 Release 构建和主 workflow 的单元测试。
- 大行性能目前有 Marker 1 MiB 单行扫描单测（Win32 Debug 454 ms）及本机 Matcher 冒烟测试（`-O2` 约 7 ms）；没有在真实 Windows 编辑器 UI 中量测打开超大文件后的滚动/重绘，也没有实机完成仅替换 EXE、右键颜色、INI、宏及新旧版本并行回归。


## 工具栏导航按钮更新（2026-11-26）

- 根据用户确认的示意图，前へ／次へ采用蓝色 16×16 方向箭头图标并保留日文文字，两侧固定分隔线宽一致；Marker 快捷区仍靠右显示。
- 补充工具栏各功能的日文悬停提示，并将右键菜单分隔线移到固定 Marker 命令前，使该命令与「マーカー」菜单相邻。
- Windows Win32 Release Actions Run [#45730035075](https://github.com/w252566747/sakura/actions/runs/45730035075) 成功，提交 `8ce793a04e481d670f645502303e94b9079efddb`。Artifact ZIP SHA-256：`d9e77b80068806329b1f52401487a2aed7ae6196416cfa3c3050319de2ffd104`。
- 已交付 `结果文件/sakura.exe`，PE32 GUI Intel 80386，2,344,448 字节，SHA-256：`7cceff5c7a51445ab7bc3edc66a4d801a9f25b272351742b97e5441856d06188`。旧文件备份为 `结果文件/历史版本/sakura-before-blue-navigation-arrows-20261126.exe`。
- `git diff --check` 通过。Doxygen 成功；Cppcheck 与 MinGW 工作流分别在 MSYS2 设置阶段失败/取消，源码检查与单元测试步骤未运行。Windows GUI 外观和鼠标悬停提示仍需在目标 Windows 桌面实机确认。


## 工具栏响应式 Marker 与箭头对称（2026-11-26）

- 固定 Marker 可见数量根据工具栏剩余宽度动态计算，不再限制为 4 个；未显示的词条汇总到「Marker +N」。
- 调整窗口宽度时会重新计算可见项；Marker 词条顺序、开关状态及颜色仍由原列表维护。
- 导航箭头使用对称像素轮廓，左右箭头互为水平镜像；删除导航右边多余的竖分隔线，左右分隔宽度保持一致。
- 源码静态检查：`git diff --check` 通过。第一轮编译发现并修复 `LONG/int` 类型不匹配；修正后 Win32 Release Actions [Run #45739041830](https://github.com/w252566747/sakura/actions/runs/45739041830) 成功，Doxygen Run #45739041843 成功。
- 构建提交：`3ecd6b0b1d30f9ddb72b836dd7a8b6fe843635dc`。Artifact ZIP SHA-256：`4a947c0867eb2ad2df4f42a5ac75183a70241579cdef3c8643a194cff29a3566`；内含 PE32 GUI Intel 80386 的 `sakura.exe`，2,345,984 字节，SHA-256 `b3b4f44771dbec5af43038ab0a1c9e85940de82a698b4a5baf2df0bc5b2d10c4`。已放入 `结果文件/sakura.exe`，更新前版本已备份。
- 本轮专用 Release workflow 不含单元测试。Cppcheck/MinGW 两项独立 workflow 在 MSYS2 环境准备阶段失败，未完成其源码检查。窗口缩放时的实际 GUI 展示仍需 Windows 实机确认。
# 2026/11/26：原生导航图标、自定义 Marker 色与检查统计

- 「前へ／次へ」改用 Sakura 菜单工具栏中相同命令的原生图标；Marker 超过原有检索色数量后，分配不重复的柔和自定义底色，并由文本绘制路径直接使用同一 `COLORREF`。自定义颜色写入既有 `[WordMarker]` 数值项，没有新增共享内存字段。
- Curl 错误一览上方显示「エラー合計: N 件」，仅统计 Error，不放在「構文チェック」按钮旁。
- 新增 Marker 颜色重复载入、颜色槽耗尽后分配自定义色等 C++ 单元测试。本机便携冒烟检查验证 32 种自定义色配置互不相同、重复旧颜色可拆分；`git diff --check` 通过。
- 当前 Mac 没有 MSVC/Windows SDK；本轮 Win32 Release 编译、完整 GoogleTest 与 Windows 桌面 UI 回归尚未完成。自定义色绘制路径需由 Windows Release 构建继续验证。

