#!/usr/bin/env python3
"""Static regression checks for selected-text occurrence status integration."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
select_cpp = (ROOT / "sakura_core/view/CViewSelect.cpp").read_text(encoding="utf-8-sig")
select_h = (ROOT / "sakura_core/view/CViewSelect.h").read_text(encoding="utf-8-sig")
mouse_cpp = (ROOT / "sakura_core/view/CEditView_Mouse.cpp").read_text(encoding="utf-8-sig")
paint_cpp = (ROOT / "sakura_core/view/CEditView_Paint.cpp").read_text(encoding="utf-8-sig")

assert "PrintSelectionInfoMsg(bool includeOccurrenceCount = false) const;" in select_h
assert "void CViewSelect::PrintSelectionInfoMsg(bool includeOccurrenceCount) const" in select_cpp
assert "if (includeOccurrenceCount && !IsBoxSelecting() && !IsMouseSelecting())" in select_cpp
assert "selectedOptions.bRegularExp = false;" in select_cpp
assert "selectedOptions.bWordOnly = false;" in select_cpp
assert "selectedPattern.SetPattern" in select_cpp
assert "searchAgent.SearchWord(scanPosition, SEARCH_FORWARD, &match, selectedPattern)" in select_cpp
assert "if (match.GetFrom() == selectedStart) currentMatch = totalMatches;" in select_cpp
assert 'statusMessage.append(L" 番目 / 合計 ");' in select_cpp
assert 'statusMessage.append(L" 件)");' in select_cpp
assert "GetSelectionInfo().SelectEnd();\n\t\tGetSelectionInfo().PrintSelectionInfoMsg(true);" in mouse_cpp
assert "PrintSelectionInfoMsg(true);" in select_cpp.split("void CViewSelect::DrawSelectArea(", 1)[1].split("反転用再作画処理本体", 1)[0]
assert "GetSelectionInfo().PrintSelectionInfoMsg(true);" in paint_cpp
print("selected-text occurrence status wiring passed")
