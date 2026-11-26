#!/usr/bin/env python3
"""Compile and exercise the production scrollbar overview model on macOS."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="sakura-scrollbar-marker-") as temp:
        source = Path(temp) / "main.cpp"
        executable = Path(temp) / "scrollbar-marker-test"
        source.write_text(r'''#include "view/CScrollBarMarkerModel.h"
#include <cassert>
#include <iostream>

int main() {
  assert(CScrollBarMarkerModel::NormalizeVisibilityMask(0x3f) == 0x3f);
  assert(CScrollBarMarkerModel::NormalizeVisibilityMask(0xffffffffu) == 0x3f);
  assert(CScrollBarMarkerModel::NormalizeVisibilityMask(0x24) == 0x24);
  CScrollBarMarkerModel model;
  model.ResetOverview(100, 1000);
  model.AddOverviewMarker({100, CScrollBarMarkerModel::Kind::Search, 0x00FFA500,
    L"検索", L"timeout / 行 101", 100});
  model.AddOverviewMarker({100, CScrollBarMarkerModel::Kind::Search, 0x00FFA500,
    L"検索", L"timeout / 行 101", 100});
  model.AddOverviewMarker({100, CScrollBarMarkerModel::Kind::SyntaxError, 0x000000FF,
    L"Curl Error", L"E001: 括弧が対応していません / 行 101", 100});
  const auto& ticks = model.OverviewTicks();
  assert(ticks.size() == 2);
  assert(ticks[0].lane != ticks[1].lane);
  const std::wstring searchTooltip = CScrollBarMarkerModel::FormatTooltip(ticks[0]);
  const std::wstring syntaxTooltip = CScrollBarMarkerModel::FormatTooltip(ticks[1]);
  assert(searchTooltip.find(L"検索結果") != std::wstring::npos);
  assert(searchTooltip.find(L"2件") != std::wstring::npos);
  assert(searchTooltip.find(L"timeout") != std::wstring::npos);
  assert(searchTooltip.find(L"行 101") != std::wstring::npos);
  assert(syntaxTooltip.find(L"構文エラー") != std::wstring::npos);
  assert(syntaxTooltip.find(L"括弧が対応していません") != std::wstring::npos);
  assert(CScrollBarMarkerModel::HitTestLane(ticks, ticks[0].lane, ticks[0].y) != ticks.size());
  assert(CScrollBarMarkerModel::HitTestLane(ticks, ticks[1].lane, ticks[1].y) != ticks.size());

  CScrollBarMarkerModel compact;
  compact.ResetOverview(20, 1000000);
  for (int line = 0; line < 1000000; ++line) {
    compact.AddOverviewMarker({line, CScrollBarMarkerModel::Kind::Search, 0x00FFA500, L"検索", {}});
  }
  assert(compact.OverviewTicks().size() <= 20);
  assert(compact.OverviewTicks().front().firstLine == 0);
  assert(compact.OverviewTicks().back().lastLine == 999999);
  std::cout << "production scrollbar marker aggregation, tooltip and million-line bounds passed\n";
}
''', encoding="utf-8")
        subprocess.run([
            "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=undefined", "-I", str(ROOT / "sakura_core"),
            str(source), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
