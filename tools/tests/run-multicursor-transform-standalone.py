#!/usr/bin/env python3
"""Compile and exercise production multi-cursor edit-coordinate transforms."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
EDIT_SOURCE = (ROOT / "sakura_core/cmd/CViewCommander_Edit.cpp").read_text(encoding="utf-8-sig")
EDIT_VIEW_SOURCE = (ROOT / "sakura_core/view/CEditView_Command_New.cpp").read_text(encoding="utf-8-sig")


def main() -> None:
    undo_start = EDIT_SOURCE.index("void CViewCommander::Command_UNDO")
    redo_start = EDIT_SOURCE.index("void CViewCommander::Command_REDO", undo_start)
    undo = EDIT_SOURCE[undo_start:redo_start]
    redo_end = EDIT_SOURCE.find("\nvoid CViewCommander::", redo_start + 1)
    redo = EDIT_SOURCE[redo_start:redo_end if redo_end >= 0 else None]
    for name, body in (("Undo", undo), ("Redo", redo)):
        if "CaptureMultiCursorUndoPositions" not in body or "RestoreMultiCursorUndoPositions" not in body:
            raise SystemExit(f"multi-cursor {name} does not capture and restore cursor positions")
    if EDIT_VIEW_SOURCE.count("m_nMultiCursorSlot = m_nMultiCursorOpeSlot;") != 3:
        raise SystemExit("insert/delete/replace operations do not all capture the active cursor slot")
    if "new CMultiCursorStateOpe(state.initialCaret, state.caret" not in EDIT_SOURCE:
        raise SystemExit("multi-cursor edit groups do not retain slots without text operations")
    print("multi-cursor Undo/Redo cursor-slot wiring passed")

    with tempfile.TemporaryDirectory(prefix="sakura-multicursor-") as temp:
        temp_path = Path(temp)
        basis = temp_path / "basis"
        basis.mkdir()
        (basis / "SakuraBasis.h").write_text(r'''#pragma once
class CLogicInt {
public:
  CLogicInt(int value = 0) : value(value) {}
  operator int() const { return value; }
private:
  int value;
};
struct CLogicPoint { CLogicInt x; CLogicInt y; CLogicPoint() : x(0), y(0) {} CLogicPoint(CLogicInt x, CLogicInt y) : x(x), y(y) {} void Set(CLogicInt px, CLogicInt py) { x=px; y=py; } };
inline int PointCompare(const CLogicPoint& a, const CLogicPoint& b) {
  if (a.y != b.y) return a.y < b.y ? -1 : 1;
  if (a.x != b.x) return a.x < b.x ? -1 : 1;
  return 0;
}
''', encoding="utf-8")
        memory_stub = temp_path / "mem"
        memory_stub.mkdir()
        (memory_stub / "CNativeW.h").write_text('#pragma once\n#include "../basis/SakuraBasis.h"\nclass CNativeW {};\n', encoding="utf-8")
        (temp_path / "main.cpp").write_text(r'''#include "view/MultiCursorPointTransform.h"
#include "view/MultiCursorUndoState.h"
#include "cmd/COpe.h"
#include <array>
#include <cassert>
#include <iostream>
#include <type_traits>

static_assert(std::is_base_of<COpe, CMultiCursorStateOpe>::value, "state markers remain undo operations");
static_assert(sizeof(CMoveCaretOpe) < sizeof(CMultiCursorStateOpe), "selection payload stays out of ordinary caret operations");

static void Expect(const CLogicPoint& actual, int x, int y) {
  assert(static_cast<int>(actual.x) == x && static_cast<int>(actual.y) == y);
}

int main() {
  using multicursor::TransformPointAfterReplace;
  using multicursor::RecordUndoPosition;
  Expect(TransformPointAfterReplace({2, 1}, {4, 1}, {6, 1}, {5, 1}), 2, 1);
  Expect(TransformPointAfterReplace({9, 0}, {4, 0}, {4, 0}, {7, 0}), 12, 0);
  Expect(TransformPointAfterReplace({12, 2}, {5, 2}, {8, 2}, {6, 2}), 10, 2);
  Expect(TransformPointAfterReplace({5, 2}, {3, 2}, {8, 2}, {4, 2}), 4, 2);
  Expect(TransformPointAfterReplace({6, 8}, {2, 3}, {5, 6}, {1, 4}), 6, 6);
  Expect(TransformPointAfterReplace({15, 3}, {2, 1}, {8, 3}, {2, 5}), 9, 5);
  std::array<multicursor::UndoPosition<CLogicPoint>, 3> undoPositions{};
  std::array<multicursor::UndoPosition<CLogicPoint>, 3> redoPositions{};
  RecordUndoPosition(undoPositions, 0, {10, 0}, {11, 0}, {10, 0}, {11, 0}, false, false, false, false);
  RecordUndoPosition(undoPositions, 1, {4, 2}, {5, 2}, {2, 2}, {5, 2}, true, false, true, false);
  RecordUndoPosition(undoPositions, 1, {5, 2}, {7, 2}, {5, 2}, {7, 2}, false, false, false, false);
  RecordUndoPosition(undoPositions, 1, {4, 2}, {7, 2}, {2, 2}, {7, 2}, true, false, true, false);
  RecordUndoPosition(undoPositions, 2, {8, 3}, {8, 3}, {3, 3}, {3, 3}, true, true, true, false);
  RecordUndoPosition(redoPositions, 0, {10, 0}, {11, 0}, {10, 0}, {11, 0}, false, false, false, true);
  RecordUndoPosition(redoPositions, 1, {4, 2}, {5, 2}, {2, 2}, {5, 2}, true, false, true, true);
  RecordUndoPosition(redoPositions, 1, {5, 2}, {7, 2}, {5, 2}, {7, 2}, false, false, false, true);
  RecordUndoPosition(redoPositions, 1, {4, 2}, {7, 2}, {2, 2}, {7, 2}, true, false, true, true);
  RecordUndoPosition(redoPositions, 2, {8, 3}, {8, 3}, {3, 3}, {3, 3}, true, true, true, true);
  RecordUndoPosition(redoPositions, 99, {0, 0}, {1, 0}, {0, 0}, {1, 0}, false, false, true, true);
  assert(undoPositions[1].valid && static_cast<int>(undoPositions[1].caret.x) == 4);
  assert(undoPositions[1].hasSelection && static_cast<int>(undoPositions[1].anchor.x) == 2);
  assert(redoPositions[1].valid && static_cast<int>(redoPositions[1].caret.x) == 7);
  assert(!redoPositions[1].hasSelection);
  assert(undoPositions[2].valid && undoPositions[2].hasSelection && static_cast<int>(undoPositions[2].anchor.x) == 3);
  assert(redoPositions[2].valid && redoPositions[2].hasSelection && static_cast<int>(redoPositions[2].anchor.x) == 3);
  assert(!redoPositions[0].hasStateMarker);
  std::cout << "production multi-cursor coordinate and undo-state helpers passed\n";
}
''', encoding="utf-8")
        executable = temp_path / "multicursor-test"
        subprocess.run([
            "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
            "-I", str(temp_path), "-I", str(ROOT / "sakura_core"),
            str(temp_path / "main.cpp"), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
