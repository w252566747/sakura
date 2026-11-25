
## 2026/11/25 第二轮界面无显示修复

- 根因 1：Marker/Curl 子按钮在 Rebar 默认窗口过程完成布局前读取宽度；初始尺寸或窗口变化时可能按旧客户区尺寸隐藏。现改为默认处理后再对 `WM_SIZE`、`WM_WINDOWPOSCHANGED`、`WM_SHOWWINDOW` 重新布局。
- 根因 2：检索计数遍历把命中范围的布局坐标直接作为下一轮搜索的逻辑索引；长行自动换行时扫描位置错误，可能导致当前命中序号未计算并跳过状态栏输出。现将命中结束点转回逻辑坐标，并按下一布局行偏移换算索引。
- 远程验证：Win32 Release 构建成功，Run `45581943568`，Commit `c3a67c1b0f1df1d918a191f553558efd567ca3d5`。产物 ZIP SHA-256：`9b953f12e59fa6b0b8d5579f8af1a509b2d388408c72744c00690760c8204d48`。
- 新 `sakura.exe` 已下载并写入 `结果文件/sakura.exe`；PE32 Intel 80386，文件 SHA-256：`073ee04e8996cf59e43b629621215756a78311504a500d731eabd21f3af72ff3`。替换前版本保存在 `结果文件/历史版本/sakura-previous-build-45578668282.exe`。
- 本机 `git diff --check` 通过；Curl checker C++17 冒烟编译此前通过。
- 尚未验证：Windows GUI 实机显示、状态栏最终文案/尺寸、颜色右键即时绘制和整套交互回归。构建成功不等于桌面运行视觉验收。

## 2026/11/25 第三轮界面定位修正

- 截图对应的前一版 EXE 仍无右侧控件。复核源代码后确认，前一轮履历虽写明“主窗口子控件”，实际主工作区仍将按钮创建为 Rebar 子控件，定位也相对 Rebar；因此未占用 Rebar 右侧属于主窗口的留白区域。
- 本轮将 Marker 与 Curl 按钮统一创建为主编辑窗口子控件，按原生工具栏/ Rebar 的屏幕坐标换算到主窗口坐标进行排布。保留原生工具栏子类化处理；主窗口右键与销毁消息交还默认窗口过程。
- `git diff --check` 通过。
- Win32 Release：Run `45615387389`，Commit `159389e88ba34f439dcbfd79abd9c75d6964112c`，构建与上传步骤成功。ZIP SHA-256：`677e4ed6fdfb6820d923418c3f262407d3310c535ba8c1bdefb595358ba72a00`。
- 新 `结果文件/sakura.exe` 已替换，PE32 Intel 80386，大小 2,315,776 字节，SHA-256：`1121064ed822f664fff2fcd067133bd9157d4ee82797cbf3668d0ab2bcee9c37`。上一版保存在 `结果文件/历史版本/sakura-before-marker-ui-fix-20261125.exe`。
- 尚未完成：Windows 桌面 GUI 实机确认。用户需关闭所有旧 Sakura 进程后启动此路径的新 EXE；Actions 成功只能证明编译和打包，不证明窗口视觉结果。

## 2026/11/25 第四轮横向坐标修正

- 用户以新版本 2.4.3.7201 复验后，右侧控件仍未显示，证明上一轮的“控件被 Rebar 裁切”修复不足。
- 进一步检查发现布局起点仍可能读取被 Rebar 拉伸后的工具栏 HWND 右边界。若它覆盖整行，按钮会被放在主窗口客户区外。现改为读取工具栏最后一个实际按钮的矩形，并换算到主窗口坐标，作为构文检查按钮的起点；Marker 快捷区继续按主窗口右边缘布局。
- 第四轮修改副本：`分支修改/第四轮修改副本/sakura_core/window/CMainToolBar.cpp`；修改前备份：`分支修改/主工作区合并前备份/20261125_第四轮/CMainToolBar.cpp`。
- 当前静态检查 `git diff --check` 通过。Win32 Release 编译与 GUI 显示复验待本轮执行。
- Win32 Release 已成功：Run `45617969993`，Commit `59dff0bdab362a2615005bfc330ef1f0683517d7`；ZIP SHA-256：`092e051e94b5da2e2fc15a4a4cf994eb682a2bf4407155ed08b3b64ee1aa4c17`。
- 第四轮新 EXE 已更新到 `结果文件/sakura.exe`，PE32 Intel 80386，2,315,776 字节；SHA-256：`0e39433491bb46ca6f8b42a80158f133b5c631cc5c578ca82b4aa4e126339cb6`。上一轮 EXE 已备份为 `结果文件/历史版本/sakura-before-marker-ui-fix-round4.exe`。
- 本轮仍需 Windows GUI 复验；若仍无控件，需要通过运行时窗口坐标/窗口层级信息继续定点诊断，不能仅以编译成功判定完成。

## 2026/11/25 第五轮：关键字拼写、按钮尺寸与 Marker 导航

- Curl 检查器此前仅做词法/括号/逗号检查，不含符号词库。本轮从当前 Curl 8.0 Sakura 关键字配置合并 601 个内置名称；对代码块中与已知词只差一个字符的标识符给出 `CURL012` 拼写警告和候选名。未知自定义名称保留，不会仅因“不在词库”判错。该检查是拼写提示，不替代 Curl RTE 编译器。
- 增加 `TextFeld`→`TextField`、`define-procc`→`define-proc` 及自定义名称不误报的单元用例。
- 按用户要求把“前へ/次へ”放到“構文チェック”右侧；按钮宽度缩小（构文 96 DPI 单位、导航各 48、Marker/全解除更窄，词条按钮最大 132）。
- Marker 前后跳转现在移动到词条起点，并通过 Sakura 原生选择区设置完整词条范围和重绘；下一次跳转从当前词条继续。
- 本地 C++17 独立冒烟用例通过：两种拼写错误均告警、无关自定义类名不告警、合法 Curl 样例通过。2.5 MiB / 100,000 行扫描耗时 46 ms（本机优化构建，不等同于 Windows 实际运行基准）。`git diff --check` 通过。
- 修改副本位于 `分支修改/第五轮修改副本/`，本轮合并前备份位于 `分支修改/主工作区合并前备份/20261125_第五轮/`。
- Win32 Release 构建和 Windows GUI 选择显示验证待执行；Windows 单元测试运行状态也待 CI/Windows 环境确认。
- Win32 Release 构建成功：Run `45621618011`，Commit `81340fc705f05cb378e27c49c5706e3886726d30`；ZIP SHA-256：`84e364719e2bfc7214a0b6d80c917a6500903d48813503f3c542febd925847da`。
- 第五轮新 EXE 已更新至 `结果文件/sakura.exe`，PE32 Intel 80386，2,336,768 字节；SHA-256：`978bc63a7faf3dea19ba32629f900a44a4fc1f6d71e0d4766b6b8ba660cecdd1`。第四轮版本备份为 `结果文件/历史版本/sakura-before-marker-fifth-round-20261125.exe`。
- 构建步骤成功不等于 Windows GUI 行为已验证；前后按钮选区显示、紧凑尺寸和真实 `.scurl` 关键字误拼检查仍需用户用新 EXE 实测。本机单元逻辑冒烟和大文件扫描基准已通过，Windows GoogleTest 尚未执行。


## 第六轮误报收敛验证（2026/11/25）

- 目标：降低普通 Curl 标识符和顶层显示文本的误报；缩小右上角 Marker 控件。
- 修改：拼写相似提示只针对 Curl `{...}` 块首名称，普通变量、参数和属性名不再与内置词库盲比；圆括号/方括号仅在 Curl 代码块内参与结构检查；支持 `new` 与类名跨行；跳过顶层文本中的转义保留符号。
- 本机独立 C++17 冒烟用例：9 类场景全部通过，覆盖自定义变量/属性、顶层括号、拼错的 `TextFeld`、跨行 `new`、转义花括号、未闭合大括号、无效字符串转义、VBox/Table 缺逗号。
- 大文件扫描：2.5 MiB、100,000 行，0 条诊断，优化构建约 24 ms（macOS 本机结果，不代表 Win32 实测）。
- 语法范围限制：该检查器仍不是 Curl RTE 8.0 编译器；目前能可靠报告部分词法/括号/常见布局逗号问题，内置名相似度仅为警告。函数重载、类型兼容、上下文相关代码块语法和属性合法性需要官方 RTE 才能判定。
- 仓库真实示例 `curl8-sakura-config/Curl8_sample.scurl` 全文件检查：0 条诊断。
- Win32 Release：GitHub Actions Run `45625231422`，Commit `655f29f8245f1b5703b0e1199ba370a3c47a4e8d`，构建和 EXE 上传成功。构建 ZIP SHA-256：`41ebf357eea74ec4a5be87be58e42ce07721b41d40f0cf94cb24a715d1383b64`。
- 输出 `结果文件/sakura.exe`：PE32 Intel 80386，2,337,792 字节，SHA-256：`32820e767497823036ab506e4aaebc20c585c4796a65ab7e2744fe5ec0582f90`。替换前版本已保存在 `结果文件/历史版本/sakura-before-compact-toolbar-round6-20261125.exe`。
- Windows GoogleTest 和实际窗口截图/按钮交互验收：当前 Mac 环境无法执行，需在 Windows 用新 EXE 验证。

## 2026/11/25 第九轮：逗号误报与拼写漏报复核

- 复现确认：`{some-proc alpha, beta, gamma}` 原来会误报 CURL003。原因是扫描器消费标识符时没有清除前一个逗号状态，普通参数序列被当成连续逗号。
- 修复：标识符消费后清除逗号状态；相邻重复逗号报告 CURL003；表达式名后立即出现开头逗号报告 CURL014；逗号后直接遇到闭合括号/大括号时报告 CURL013。合法位置参数序列通过。
- 拼写提示修复：检查 4 字符以上的表达式头（因此 `whil` 可提示 `while`）；补充代码块任意位置的语言关键字检查，以及冒号后的类型名称检查；覆盖 `break/catch/throw/while` 等词的一字母删除。验证 `class-procc`→`class-proc`、`retun`→`return`、`Strng`→`String`、`cath`→`catch` 均产生 CURL012 Warning。未修改的 GUI 属性 `width` 不产生 `with` 误报。
- 本机 C++17 独立冒烟：合法参数、布局样例无误报；开头逗号、双逗号和尾逗号分别有 CURL014/CURL003/CURL013 诊断；关键字/类型拼写样例产生预期警告；官方样例 `Curl8_sample.scurl` 仍为 0 条诊断。
- 性能：优化构建扫描 5,700,000 个字符、100,000 行，0 条诊断，约 116 ms（macOS 本机，不代表 Win32 桌面运行测量）。`git diff --check` 通过。
- 本轮已添加 GoogleTest 用例；当前 Mac 未运行仓库完整 GoogleTest，专用 Win32 工作流只编译 Release EXE，未执行 GoogleTest。当前 CI 的 MinGW 与 Cppcheck 工作流因 MSYS2 初始化失败没有运行源码测试/静态分析。
- 限制：这是保守静态检查，不是 Curl RTE 编译器；CURL012 是拼写 Warning。只对表达式头、已列语言词和类型标注做拼写提示，不能判定所有上下文相关 API/宏/自定义标识符是否合法。2～3 字符短词或未列入语言词表的拼写仍不能保证识别；未知 API/自定义名称不能仅凭词表判错。
- 官方示例 `curl8-sakura-config/Curl8_sample.scurl` 使用本轮最新代码复查：0 条诊断。

- Win32 Release 专用工作流 Run `45637975707` 成功，构建提交 `84f3fa91a4718f48aff9876855a439a94bc80fbd`；GoogleTest 未由该工作流执行。构建 ZIP SHA-256：`d4e739f8d715ec3ddc2003d932d7901e9bb9c089cf675940f905378871e66e80`。
- 交付 `结果文件/sakura.exe`：PE32 GUI Intel 80386，2,341,888 字节，SHA-256 `2d29d4173d8ce553edcb4673abbf68b0e712b48b4a2f19f62168a565a988dfa1`。旧文件备份：`结果文件/历史版本/sakura-before-curl-check-round9-20261125.exe`。
- 为缩短等待，另一个全量工作流 Run `45638107943` 已取消；这不是源码构建失败。
