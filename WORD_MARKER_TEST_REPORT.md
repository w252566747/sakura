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
| Marker 单元测试 | 已新增：固定项匹配/切换、数量上限、最长重叠匹配、颜色槽分配、文档限定及自动开关；另有 1 MiB 行扫描计时项；尚未运行 |
| 本机 Matcher 冒烟测试 | 通过：直接编译 `CWordMarker.cpp`（含 `-Wall -Wextra -Werror`），覆盖固定/自动匹配、文档隔离、最长重叠和 1 MiB 行扫描；本次 `-O2` 约 7 ms、无优化约 57 ms（当前 Mac 算法冒烟值，不代表 Win32 UI 性能） |
| 仅替换 `sakura.exe` | 静态上可行：未增加 DLL、插件、外部运行库或共享内存字段；必须在原有 Sakura 安装依赖目录中验证后才能确认 |
| Win32 Release 编译 | 未运行：当前 macOS 环境没有 MSBuild 或 MinGW Win32 工具链 |
| 单元测试执行 | 未运行：测试目标为 Windows；需在项目 Windows CI/Windows 环境执行 |
| 大文件性能、界面及回归测试 | 未运行：需 Win32 可执行程序和真实 Sakura UI |
| `sakura.exe` 产物 | 未生成 |

Marker 状态没有加入 `DLLSHAREDATA`、`CommonSetting` 或共享内存版本结构；只新增 `CPropCommon` 的进程内对话框暂存字段及 `sakura.ini` 的 `[WordMarker]` 项。

## 仍待完成

自动/固定 Marker 的底层匹配和渲染接入已在工作区实现；共通設定「全般」页新增自动 Marker 开关；日语右键菜单已加入固定项的添加/解除、单项移除、颜色轮换和全部清除入口；新安装的默认工具栏新增一个打开该菜单的入口。绘制使用现有可视区域逐字符着色流程，但还没有跨重绘缓存。独立 Marker 工具栏，以及 Windows 上编译和全量测试仍未完成。现有用户的自定义工具栏不会自动覆盖或迁移。当前改动不能作为完整交付版使用。

项目 Actions 的 Win32 Release 作业增加了独立 `sakura.exe` Artifact；待将分支推送到用户有权限的 GitHub 仓库后才能运行并取得二进制。
