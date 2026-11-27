#!/usr/bin/env python3
"""Compile and exercise the production code-fold parser without Win32 headers."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def check_hidden_line_navigation_wiring() -> None:
    manager_header = (ROOT / "sakura_core/doc/layout/CLayoutMgr.h").read_text(encoding="utf-8-sig")
    manager_source = (ROOT / "sakura_core/doc/layout/CLayoutMgr.cpp").read_text(encoding="utf-8-sig")
    bookmarks = (ROOT / "sakura_core/cmd/CViewCommander_Bookmark.cpp").read_text(encoding="utf-8-sig")
    search = (ROOT / "sakura_core/cmd/CViewCommander_Search.cpp").read_text(encoding="utf-8-sig")
    edit_window = (ROOT / "sakura_core/window/CEditWnd.cpp").read_text(encoding="utf-8-sig")
    mouse = (ROOT / "sakura_core/view/CEditView_Mouse.cpp").read_text(encoding="utf-8-sig")
    assert "bool expandHiddenLine = true" in manager_header
    begin = manager_source.index("void CLayoutMgr::LogicToLayout(")
    end = manager_source.index("void CLayoutMgr::LayoutToLogic(", begin)
    conversion = manager_source[begin:end]
    assert "IsCodeFoldHiddenLine(ptLogic.y)" in conversion
    assert "ExpandCodeFoldsContaining(ptLogic.y)" in conversion
    assert "LogicToLayout(ptXY,&ptLayout)" in bookmarks  # bookmark and function-list navigation
    assert "rLayoutMgr.LogicToLayout( CLogicPoint(nIdxTo, pcLayout->GetLogicLineNo())" in search
    assert "m_cLayoutMgr.LogicToLayout(CLogicPoint(diagnostic.column - 1, diagnostic.line - 1)" in edit_window
    assert "document->m_cLayoutMgr.LogicToLayout(logic, &layoutPosition)" in edit_window
    assert "ToggleCodeFoldRecursivelyAtLine(foldLine)" in mouse
    assert "fwKeys & MK_SHIFT" in mouse


def main() -> None:
    check_hidden_line_navigation_wiring()
    with tempfile.TemporaryDirectory(prefix="sakura-codefold-") as temp:
        temp_path = Path(temp)
        (temp_path / "StdAfx.h").write_text("#pragma once\n", encoding="utf-8")
        (temp_path / "main.cpp").write_text(r'''#include "parse/CodeFoldParser.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

static std::vector<codefold::Region> ParseText(const std::vector<std::wstring>& text, codefold::Language language) {
  std::vector<std::wstring_view> lines;
  for (const auto& line : text) lines.push_back(line);
  return codefold::Parse(lines, language);
}

int main() {
  using codefold::Language;
  const auto curl = ParseText({L"define-proc foo() {", L"  || } ignored", L"  let x = \"}\";", L"  if (x) {", L"  }", L"}"}, Language::Curl);
  assert(curl.size() == 2 && curl[0].firstLine == 0 && curl[0].lastLine == 5);
  assert(curl[1].firstLine == 3 && curl[1].lastLine == 4);

  const auto curlRaw = ParseText({L"define-proc raw() {", L"  |#", L"    } {", L"  #|",
    L"  |tag\"", L"    } {", L"  \"|tag|", L"  |4\"{{}}\"4|",
    L"  |6\"x\"6|{}\"6|", L"  if (x) {", L"  }", L"}"}, Language::Curl);
  assert(curlRaw.size() == 2 && curlRaw[0].firstLine == 0 && curlRaw[0].lastLine == 11);
  assert(curlRaw[1].firstLine == 9 && curlRaw[1].lastLine == 10);

  const auto curlMultilineString = ParseText({L"define-proc multiline() {", L"  let s = \"text",
    L"    \\{literal", L"    more literal\\}\";", L"  if (ready) {", L"  }", L"}"}, Language::Curl);
  assert(curlMultilineString.size() == 2 && curlMultilineString[0].firstLine == 0 && curlMultilineString[0].lastLine == 6);
  assert(curlMultilineString[1].firstLine == 4 && curlMultilineString[1].lastLine == 5);

  const auto curlStringExpression = ParseText({L"define-proc interpolation() {", L"  let s = \"value {",
    L"    {VBox", L"      {TextField}", L"    }", L"  } tail\";", L"}"}, Language::Curl);
  assert(curlStringExpression.size() == 3);
  assert(curlStringExpression[0].firstLine == 0 && curlStringExpression[0].lastLine == 6);
  assert(curlStringExpression[1].firstLine == 1 && curlStringExpression[1].lastLine == 5);
  assert(curlStringExpression[2].firstLine == 2 && curlStringExpression[2].lastLine == 4);

  const auto curlEscapedBraces = ParseText({L"define-proc escaped() {", L"  \\{", L"  literal text",
    L"  \\}", L"}"}, Language::Curl);
  assert(curlEscapedBraces.size() == 1 && curlEscapedBraces[0].firstLine == 0 && curlEscapedBraces[0].lastLine == 4);

  const auto cpp = ParseText({L"void f() {", L"  /* }", L"     { */", L"  auto s = R\"tag({})tag\";", L"  if (ok) {", L"  }", L"}"}, Language::CLike);
  assert(cpp.size() == 2 && cpp[0].firstLine == 0 && cpp[0].lastLine == 6);
  assert(cpp[1].firstLine == 4 && cpp[1].lastLine == 5);
  const auto marked = ParseText({L"#pragma region User section", L"int value = 1;", L"#pragma endregion"}, Language::CLike);
  assert(marked.size() == 1 && marked[0].firstLine == 0 && marked[0].lastLine == 2);

  const auto java = ParseText({L"class A {", L"  void f() {", L"    String text = \"\"\"", L"      } {", L"      \"\"\";", L"    if (ready) {", L"    }", L"  }", L"}"}, Language::Java);
  assert(java.size() == 3 && java[0].firstLine == 0 && java[0].lastLine == 8);
  assert(java[1].firstLine == 1 && java[1].lastLine == 7);
  assert(java[2].firstLine == 5 && java[2].lastLine == 6);

  const auto javascript = ParseText({L"function f() {", L"  const text = `not a block { ${value} }`;", L"  if (ready) {", L"  }", L"}"}, Language::JavaScript);
  assert(javascript.size() == 2 && javascript[0].firstLine == 0 && javascript[0].lastLine == 4);
  assert(javascript[1].firstLine == 2 && javascript[1].lastLine == 3);
  const auto json = ParseText({L"{", L"  \"text\": \"escaped \\\"}\\\" brace\",", L"  \"nested\": {", L"  }", L"}"}, Language::Json);
  assert(json.size() == 2 && json[0].firstLine == 0 && json[0].lastLine == 4);
  assert(json[1].firstLine == 2 && json[1].lastLine == 3);
  const auto jsonArray = ParseText({L"[", L"  {", L"    \"id\": 1", L"  }", L"]"}, Language::Json);
  assert(jsonArray.size() == 2 && jsonArray[0].firstLine == 0 && jsonArray[0].lastLine == 4);
  assert(jsonArray[1].firstLine == 1 && jsonArray[1].lastLine == 3);

  const auto xml = ParseText({L"<root>", L"  <!-- <bad> -->", L"  <item>", L"  </item>", L"</root>"}, Language::Xml);
  assert(xml.size() == 2 && xml[0].firstLine == 0 && xml[0].lastLine == 4);
  assert(xml[1].firstLine == 2 && xml[1].lastLine == 3);

  const auto sql = ParseText({L"BEGIN", L"  v := 'END;';", L"  IF ok THEN", L"    NULL;", L"  END IF;", L"END;"}, Language::Sql);
  assert(sql.size() == 2 && sql[0].firstLine == 0 && sql[0].lastLine == 5);
  assert(sql[1].firstLine == 2 && sql[1].lastLine == 4);

  const auto invalid = ParseText({L"void f() {", L"  if (true) {"}, Language::CLike);
  assert(invalid.empty());

  const auto mismatchedSql = ParseText({L"IF ready THEN", L"  BEGIN", L"    NULL;", L"END IF;", L"END;"}, Language::Sql);
  assert(mismatchedSql.size() == 1 && mismatchedSql[0].firstLine == 1 && mismatchedSql[0].lastLine == 4);
  const auto qQuotedSql = ParseText({L"BEGIN", L"  v := q'[END; IF fake THEN]';", L"  v := Q'{ END LOOP; }';", L"  IF ready THEN", L"    NULL;", L"  END IF;", L"END;"}, Language::Sql);
  assert(qQuotedSql.size() == 2 && qQuotedSql[0].firstLine == 0 && qQuotedSql[0].lastLine == 6);
  assert(qQuotedSql[1].firstLine == 3 && qQuotedSql[1].lastLine == 5);
  const auto sqlCaseExpression = ParseText({L"SELECT CASE WHEN active = 1 THEN 'yes' ELSE 'no' END AS state", L"FROM sample;"}, Language::Sql);
  assert(sqlCaseExpression.empty());
  const auto multilineQQuote = ParseText({L"BEGIN", L"  v := q'!", L"    END; IF fake THEN", L"  !';", L"END;"}, Language::Sql);
  assert(multilineQQuote.size() == 1 && multilineQQuote[0].firstLine == 0 && multilineQQuote[0].lastLine == 4);
  std::cout << "production code-fold parser Curl/C-like/XML/SQL cases passed\n";
}
''', encoding="utf-8")
        executable = temp_path / "codefold-test"
        subprocess.run([
            "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
            "-I", str(temp_path), "-I", str(ROOT / "sakura_core"),
            str(ROOT / "sakura_core/parse/CodeFoldParser.cpp"), str(temp_path / "main.cpp"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
