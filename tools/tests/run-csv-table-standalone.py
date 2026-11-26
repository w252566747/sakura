#!/usr/bin/env python3
"""Compile and exercise the production CSV parser and table model."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="sakura-csv-table-") as temp:
        (Path(temp) / "StdAfx.h").write_text("#pragma once\n", encoding="utf-8")
        source = Path(temp) / "main.cpp"
        executable = Path(temp) / "csv-table-test"
        source.write_text(r'''#include "parse/CSVTableModel.h"
#include <cassert>
#include <iostream>

static std::wstring Apply(const std::wstring& source, const csvtable::TextEditPatch& patch) {
  assert(patch.valid && patch.begin <= source.size() && patch.deleteLength <= source.size() - patch.begin);
  std::wstring result = source;
  result.replace(patch.begin, patch.deleteLength, patch.replacement);
  return result;
}

int main() {
  using namespace csvtable;
  const std::wstring text = L"id,name,qty\r\n10,\"A, one\",5\r\n2,B,12";
  auto snapshot = CSVTableSnapshot::Create(text, {}, 73);
  assert(snapshot && snapshot->DocumentVersion() == 73);
  CSVTableModel model(snapshot, true);
  assert(model.RowCount() == 2 && model.ColumnCount() == 3);
  assert(model.CellText(0, 1) == L"A, one");

  const auto edit = model.PrepareCellEdit(0, 1, L"A, one, revised");
  const auto edited = Apply(text, edit);
  const auto editedSnapshot = CSVTableSnapshot::Create(edited);
  CSVTableModel editedModel(editedSnapshot, true);
  assert(editedModel.CellText(0, 1) == L"A, one, revised");
  assert(edited.find(L"\r\n2,B,12") != std::wstring::npos);

  const auto append = model.PrepareAppendRow({L"3", L"C", L"0"});
  const auto appended = Apply(text, append);
  const auto appendedSnapshot = CSVTableSnapshot::Create(appended);
  CSVTableModel appendedModel(appendedSnapshot, true);
  assert(appendedModel.RowCount() == 3 && appendedModel.CellText(2, 0) == L"3");

  const auto removedColumn = model.PrepareDeleteColumn(1);
  assert(removedColumn.size() == 3);
  std::wstring withoutColumn = text;
  for (const auto& patch : removedColumn) withoutColumn = Apply(withoutColumn, patch);
  const auto compactSnapshot = CSVTableSnapshot::Create(withoutColumn);
  CSVTableModel compactModel(compactSnapshot, true);
  assert(compactModel.ColumnCount() == 2);
  assert(compactModel.CellText(0, 1) == L"5" && compactModel.CellText(1, 1) == L"12");

  const auto natural = model.BuildViewOrder({}, FilterLogic::And, {{0, SortKind::Natural, false}});
  assert(!natural.identity && natural.rows.size() == 2 && natural.rows[0] == 1 && natural.rows[1] == 0);
  const auto numericFilter = model.BuildViewOrder({{2, FilterOperator::NumberGreater, L"5"}},
    FilterLogic::And, {});
  assert(numericFilter.rows.size() == 1 && numericFilter.rows[0] == 1);
  const auto invalidRegex = model.BuildViewOrder({{1, FilterOperator::Regex, L"["}}, FilterLogic::And, {});
  assert(invalidRegex.invalidFilter == 0);

  int checks = 0;
  const auto cancelled = model.BuildViewOrder({{1, FilterOperator::Contains, L"A"}}, FilterLogic::And, {},
    [&checks] { return ++checks > 0; });
  assert(cancelled.cancelled && cancelled.rows.empty());
  std::cout << "production CSV parser, source-preserving patches, sorting and filters passed\n";
}
''', encoding="utf-8")
        subprocess.run([
            "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=undefined", "-I", temp, "-I", str(ROOT / "sakura_core"),
            str(ROOT / "sakura_core/parse/CSVParser.cpp"),
            str(ROOT / "sakura_core/parse/CSVTableModel.cpp"), str(source),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
