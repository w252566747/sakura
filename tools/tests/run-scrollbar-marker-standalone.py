#!/usr/bin/env python3
"""Compile and exercise the production scrollbar overview model on macOS."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def check_diagnostic_cache_invalidation() -> None:
    document_header = (ROOT / "sakura_core/doc/CEditDoc.h").read_text(encoding="utf-8-sig")
    edit_window = (ROOT / "sakura_core/window/CEditWnd.cpp").read_text(encoding="utf-8-sig")
    scroll_view = (ROOT / "sakura_core/view/CEditView_Scroll.cpp").read_text(encoding="utf-8-sig")
    assert "m_uCurlSyntaxDiagnosticsGeneration" in document_header
    assert edit_window.count("++document->m_uCurlSyntaxDiagnosticsGeneration;") == 3
    assert "mix(m_pcEditDoc->m_uCurlSyntaxDiagnosticsGeneration);" in scroll_view


def check_visibility_gates_background_scan() -> None:
    scroll_view = (ROOT / "sakura_core/view/CEditView_Scroll.cpp").read_text(encoding="utf-8-sig")
    scan = scroll_view.split("void CEditView::UpdateScrollBarMarkerScan()", 1)[1].split(
        "void CEditView::DrawScrollBarMarkers", 1
    )[0]
    assert "const unsigned visibility = GetScrollBarMarkerVisibility();" in scan
    assert "if (visibility == 0)" in scan and "ResetScrollBarMarkerScan();" in scan
    assert "mix(static_cast<uint64_t>(visibility));" in scan
    assert "if (visibility & searchBit)" in scan
    assert "if (visibility & wordMarkerBit) mix(markerService.Generation());" in scan
    assert "if (visibility & bookmarkBit) mix(GetBookmarkGeneration());" in scan
    assert "if (visibility & syntaxBits)" in scan
    assert "if (visibility & modifiedBit)" in scan
    assert "const bool searchActive = (visibility &" in scan
    assert "const bool syntaxActive = (visibility &" in scan
    assert "((visibility & searchBit) && m_bCurSearchUpdate)" in scan
    assert "if (!(visibility & kindBit)) continue;" in scan
    assert "CScrollBarMarkerModel::Kind::Bookmark" in scan
    assert "CScrollBarMarkerModel::Kind::Modified" in scan
    assert "if (!scanWordMarkers) m_nScrollBarMarkerScanOffset = length;" in scan
    assert "while (scanWordMarkers &&" in scan
    setter = scroll_view.split("void CEditView::SetScrollBarMarkerVisibility(unsigned mask)", 1)[1].split(
        "unsigned CEditView::GetScrollBarMarkerColor", 1
    )[0]
    assert "view.ResetScrollBarMarkerScan();" in setter
    assert "::KillTimer(view.GetHwnd(), IDT_SCROLLBAR_MARKERS);" in setter
    assert "::SetTimer(view.GetHwnd(), IDT_SCROLLBAR_MARKERS, 100, EditViewTimerProc);" in setter
    assert "if (GetScrollBarMarkerVisibility() != 0)" in scroll_view


def main() -> None:
    check_diagnostic_cache_invalidation()
    check_visibility_gates_background_scan()
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
