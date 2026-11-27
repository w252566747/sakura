#!/usr/bin/env python3
"""Compile the cursor-limit validator and verify the settings-page wiring."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def check_settings_page_wiring() -> None:
    property_page = (ROOT / "sakura_core/prop/CPropComEdit.cpp").read_text(encoding="utf-8-sig")
    property_state = (ROOT / "sakura_core/prop/CPropCommon.cpp").read_text(encoding="utf-8-sig")
    resource = (ROOT / "sakura_core/sakura_rc.rc").read_text(encoding="utf-16")
    resource_ids = (ROOT / "src/main/resources/sakura_rc.h").read_text(encoding="utf-8-sig")
    settings_io = (ROOT / "sakura_core/env/CShareData_IO.cpp").read_text(encoding="utf-8-sig")
    assert "multicursor::IsValidCursorLimit(maxMultiCursors)" in property_page
    assert "IDC_EDIT_MULTICURSOR_MAXCOUNT" in property_page and "InfoMessage" in property_page
    assert "SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, TRUE)" in property_page
    assert "SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, FALSE)" in property_page
    assert "m_nMaxMultiCursors = CEditView::GetMaxMultiCursors()" in property_state
    assert "CEditView::SetMaxMultiCursors(m_nMaxMultiCursors)" in property_state
    assert '"マルチカーソル"' in resource and "IDC_EDIT_MULTICURSOR_MAXCOUNT" in resource
    assert "#define IDC_EDIT_MULTICURSOR_MAXCOUNT    1744" in resource_ids
    assert 'L"MultiCursor", L"MaxCount"' in settings_io


def main() -> None:
    check_settings_page_wiring()
    with tempfile.TemporaryDirectory(prefix="sakura-multicursor-settings-") as temp:
        source = Path(temp) / "main.cpp"
        executable = Path(temp) / "multicursor-settings-test"
        source.write_text(r'''#include "view/MultiCursorSettings.h"
#include <cassert>
#include <iostream>
int main() {
  assert(!multicursor::IsValidCursorLimit(0));
  assert(multicursor::IsValidCursorLimit(1));
  assert(multicursor::IsValidCursorLimit(1000));
  assert(!multicursor::IsValidCursorLimit(1001));
  std::cout << "multi-cursor limit validation boundaries passed\n";
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
