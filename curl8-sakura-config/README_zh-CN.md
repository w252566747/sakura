# Sakura Editor Curl 8.0 高亮配置包（自定义配置，非官方）

目标是 Sakura Editor 2.4.3.7173 GHA 32 位，基准提交 `fe1a4a281c6e6bec2a1a1bc39059fe7424bb5075`。只使用原生类型配置、关键字和正则关键字；背景沿用 Sakura 默认米白色（RGB `#FFFBF0`），不再设为纯白；不修改 sakura.exe，不改 Windows 默认关联。

## 导入

1. 将整个目录放在固定位置，保持 `.ini`、`.kwd` 和补完词库同目录。
2. Sakura 菜单：`設定(O)` → `タイプ別設定一覧(Y)`。
3. 点击列表的 `インポート(I)`，选择 `Curl8.ini`。首次安装时，确认窗口默认的 `新規追加(&A)` 会新增 Curl 类型。
4. **如果已经有旧的 `Curl 8.0` 类型（例如之前装过白底版），更新时先在列表选中旧 `Curl 8.0` 再点 `インポート(I)`；确认窗口选择第二个单选项 `Curl 8.0`（不是 `新規追加(&A)`），把配置导入到现有类型，避免留下重复类型。颜色下拉框选择首项 `--そのままインポート--`，使用 INI 内的颜色和米白底色。
5. 检查类型名为 `Curl 8.0`，扩展名为 `curl,dcurl,mcurl,scurl,xcurl,pcurl`。不要勾选右键菜单/双击打开等 Windows 关联选项。
6. 打开 `Curl8_sample.scurl` 验证；执行 Sakura 的输入补完命令查看候选。

`.rkw` 已经嵌在 `.ini` 中，无需重复导入。单独 `.col` 仅用于单独导入颜色，单独 `.rkw` 仅用于单独导入正则关键字。

删除新增配置可在类型列表选中 `Curl 8.0` 后点 `削除`。

导入后需要重新打开目标文件，或对当前文件执行类型临时应用。默认补完来自导入的关键字集合。如需使用单独词库，可在新类型的 `設定変更` → `支援` 页面指定 `Curl8_Autocomplete.txt` 的完整路径。

## 官方资料

- Curl 8.0：[文件类型与扩展名](https://www.curlap.com/support/developers/curl/docs/rte/8.0/ja/docs/ja/dguide/files.html)、[基本语法与注释](https://www.curlap.com/support/developers/curl/docs/rte/8.0/ja/docs/ja/dguide/basic-syntax.html)。
- Sakura：[类型别设置列表与导入](https://sakura-editor.github.io/help/HLP000072.html)、[设置文件格式说明](https://sakura-editor.github.io/help/HLP000261.html)、[类型导入确认](https://sakura-editor.github.io/help/HLP000338.html)。

## 文件和限制

完整格式说明、支持边界及测试明细见 [README_ja.md](README_ja.md) 和 [TEST_REPORT.md](TEST_REPORT.md)。该词库覆盖常用语法和 GUI/API 核心名称，不能宣称穷尽官方所有 API。`||` 与 `|#...#|` 由原生注释规则处理；单行标签注释有正则辅助，跨行标签及长度指定注释无法由原生配置完整解析。横杠标识符通过 BREGEXP 兼容正则按词边界着色；设备若缺 Sakura 自带正则库则无法显示此项。
