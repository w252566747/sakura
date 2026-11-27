#!/usr/bin/env python3
"""Compile enhancement feature settings and verify Win32 wiring."""
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def check_toolbar_wiring() -> None:
    toolbar = (ROOT / "sakura_core/window/CMainToolBar.cpp").read_text(encoding="utf-8-sig")
    settings_io = (ROOT / "sakura_core/env/CShareData_IO.cpp").read_text(encoding="utf-8-sig")
    assert "ShowEnhancementToolbarMenu(point)" in toolbar
    assert "TB_HIDEBUTTON" in toolbar
    for group in ("CurlSyntax", "RainbowBracket", "MarkerNavigation", "RealtimeFilter", "CSVTable",
                  "CodeFolding", "MultiCursor"):
        assert f"enhancementtoolbar::Group::{group}" in toolbar
    assert "buttons[3].idCommand = CODEFOLD_BUTTON" in toolbar
    assert "buttons[4].idCommand = MULTICURSOR_BUTTON" in toolbar
    assert "F_MULTICURSOR_SELECT_NEXT" in toolbar and "F_MULTICURSOR_ADD_DOWN" in toolbar
    assert "CODEFOLD_BUTTON = F_CODEFOLD_TOGGLE" in toolbar
    assert 'L"EnhancementToolbar", L"VisibilityMask"' in settings_io
    assert "enhancementtoolbar::SetVisibilityMask" in settings_io
    assert "enhancementtoolbar::GetVisibilityMask" in settings_io


def check_runtime_feature_wiring() -> None:
    settings = (ROOT / "sakura_core/window/EnhancementToolbarSettings.h").read_text(encoding="utf-8-sig")
    edit_window = (ROOT / "sakura_core/window/CEditWnd.cpp").read_text(encoding="utf-8-sig")
    settings_io = (ROOT / "sakura_core/env/CShareData_IO.cpp").read_text(encoding="utf-8-sig")
    layout = (ROOT / "sakura_core/doc/layout/CLayoutMgr.cpp").read_text(encoding="utf-8-sig")
    rainbow_layout = (ROOT / "sakura_core/doc/layout/CLayoutMgr_DoLayout.cpp").read_text(encoding="utf-8-sig")
    view_scroll = (ROOT / "sakura_core/view/CEditView_Scroll.cpp").read_text(encoding="utf-8-sig")
    commander = (ROOT / "sakura_core/cmd/CViewCommander_Edit.cpp").read_text(encoding="utf-8-sig")
    syntax_service = (ROOT / "sakura_core/curl/CurlSyntaxCheckService.cpp").read_text(encoding="utf-8-sig")
    assert "DEFAULT_ENABLED_MASK = KNOWN_VISIBILITY_MASK" in settings
    assert 'L"EnhancementFeatures", L"EnabledMask"' in settings_io and "SetFeatureEnabled" in settings_io
    assert "RefreshCurlSyntaxRealtime" in edit_window and "RefreshScrollBarMarkerFeature" in edit_window
    assert "ToggleRealtimeFilter();" in edit_window and "ToggleCSVTableMode();" in edit_window
    assert "ResetCodeFolds();" in edit_window
    assert "Group::CodeFolding" in layout
    assert "Group::RainbowBracket" in rainbow_layout and "Group::MarkerNavigation" in view_scroll
    assert "Group::MultiCursor" in commander and "ClearExtraCursors" in edit_window
    assert "IsRealtimePreferenceEnabled" in syntax_service and "SetFeatureEnabled" in syntax_service
    assert "GetScrollBarMarkerConfiguredVisibility" in settings_io


def check_common_settings_page() -> None:
    common = (ROOT / "sakura_core/prop/CPropCommon.cpp").read_text(encoding="utf-8-sig")
    header = (ROOT / "sakura_core/prop/CPropCommon.h").read_text(encoding="utf-8-sig")
    resources = (ROOT / "sakura_core/sakura_rc.rc").read_text(encoding="utf-16")
    resource_ids = (ROOT / "src/main/resources/sakura_rc.h").read_text(encoding="utf-8-sig")
    assert "CPropEnhancement::DlgProc_page" in common
    assert "enhancementtoolbar::SetVisibilityMask(m_uEnhancementToolbarVisibilityMask);" in common
    assert "m_uEnhancementToolbarVisibilityMask = enhancementtoolbar::GetVisibilityMask();" in common
    assert "header->code == PSN_KILLACTIVE" in common and "DWLP_MSGRESULT, FALSE" in common
    get_data = common.split("int CPropEnhancement::GetData", 1)[1].split("\n}", 1)[0]
    assert "m_uEnhancementToolbarVisibilityMask = mask;" in get_data
    assert "m_uEnhancementFeatureEnabledMask = mask;" in get_data
    assert "SetVisibilityMask" not in get_data
    assert "class CPropEnhancement final" in header
    assert "IDD_PROP_EXTENSION DIALOGEX" in resources
    assert "STR_PROPCOMMON_EXTENSION" in resources
    assert "#define IDD_PROP_EXTENSION" in resource_ids
    for control in (
        "CURL", "RAINBOW", "MARKER", "FILTER", "CSV", "FOLD", "MULTICURSOR"
    ):
        control_id = f"IDC_CHECK_EXT_TOOLBAR_{control}"
        assert control_id in common and control_id in resources and control_id in resource_ids
    definitions = {
        name: int(value)
        for name, value in re.findall(r"^#define\s+(\w+)\s+(\d+)\s*$", resource_ids, re.M)
    }
    controls = [f"IDC_CHECK_EXT_TOOLBAR_{name}" for name in (
        "CURL", "RAINBOW", "MARKER", "FILTER", "CSV", "FOLD", "MULTICURSOR"
    )]
    control_values = [definitions[name] for name in controls]
    assert control_values == list(range(1749, 1756))
    assert len(set(control_values)) == len(control_values)
    feature_controls = [f"IDC_CHECK_EXT_FEATURE_{name}" for name in (
        "CURL", "RAINBOW", "MARKER", "FILTER", "CSV", "FOLD", "MULTICURSOR"
    )]
    feature_values = [definitions[name] for name in feature_controls]
    assert feature_values == list(range(1756, 1763))
    assert len(set(control_values + feature_values)) == 14
    for name in feature_controls:
        assert name in common and name in resources
    assert definitions["_APS_NEXT_CONTROL_VALUE"] == 1763
    settings_io = (ROOT / "sakura_core/env/CShareData_IO.cpp").read_text(encoding="utf-8-sig")
    assert 'L"EnhancementFeatures", L"EnabledMask"' in settings_io
    assert "enhancementtoolbar::SetEnabledMask" in settings_io
    assert "enhancementtoolbar::GetEnabledMask" in settings_io


def main() -> None:
    check_toolbar_wiring()
    check_common_settings_page()
    check_runtime_feature_wiring()
    with tempfile.TemporaryDirectory(prefix="sakura-enhancement-toolbar-") as temp:
        source = Path(temp) / "main.cpp"
        executable = Path(temp) / "toolbar-settings-test"
        source.write_text(r'''#include "window/EnhancementToolbarSettings.h"
#include <cassert>
#include <iostream>
int main() {
  using enhancementtoolbar::Group;
  assert(enhancementtoolbar::GetVisibilityMask() == enhancementtoolbar::DEFAULT_VISIBILITY_MASK);
  assert(enhancementtoolbar::IsVisible(Group::CurlSyntax));
  assert(enhancementtoolbar::GetEnabledMask() == enhancementtoolbar::DEFAULT_ENABLED_MASK);
  assert(enhancementtoolbar::IsEnabled(Group::CSVTable));
  enhancementtoolbar::SetEnabledMask(0u);
  assert(!enhancementtoolbar::IsEnabled(Group::CSVTable));
  enhancementtoolbar::SetEnabledMask(0xffffffffu);
  assert(enhancementtoolbar::GetEnabledMask() == enhancementtoolbar::KNOWN_ENABLED_MASK);
  enhancementtoolbar::SetEnabledMask(0u);
  assert(!enhancementtoolbar::IsEnabled(Group::CodeFolding));
  enhancementtoolbar::SetVisible(Group::RealtimeFilter, false);
  assert(!enhancementtoolbar::IsVisible(Group::RealtimeFilter));
  assert(enhancementtoolbar::GetVisibilityMask() == 0x77u);
  enhancementtoolbar::SetVisibilityMask(0xffffffffu);
  assert(enhancementtoolbar::GetVisibilityMask() == enhancementtoolbar::KNOWN_VISIBILITY_MASK);
  enhancementtoolbar::SetVisibilityMask(0u);
  assert(enhancementtoolbar::GetVisibilityMask() == 0u);
  enhancementtoolbar::SetVisible(Group::CSVTable, true);
  assert(enhancementtoolbar::IsVisible(Group::CSVTable));
  std::cout << "enhancement toolbar and runtime feature settings boundaries passed\n";
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
