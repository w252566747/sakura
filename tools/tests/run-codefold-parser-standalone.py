#!/usr/bin/env python3
"""Compile and exercise the production code-fold parser without Win32 headers."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="sakura-codefold-") as temp:
        temp_path = Path(temp)
        (temp_path / "StdAfx.h").write_text("#pragma once\n", encoding="utf-8")
        (temp_path / "main.cpp").write_text(r'''#include "parse/CodeFoldParser.h"
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

  const auto cpp = ParseText({L"void f() {", L"  /* }", L"     { */", L"  auto s = R\"tag({})tag\";", L"  if (ok) {", L"  }", L"}"}, Language::CLike);
  assert(cpp.size() == 2 && cpp[0].firstLine == 0 && cpp[0].lastLine == 6);
  assert(cpp[1].firstLine == 4 && cpp[1].lastLine == 5);

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
