#!/usr/bin/env python3
"""Static integration checks for the Win32 realtime-filter UI wiring."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "sakura_core/window/CEditWnd.cpp").read_text(encoding="utf-8-sig")


def require(fragment: str, description: str) -> None:
    if fragment not in source:
        raise SystemExit(f"missing realtime-filter integration: {description}")


# Filtering hides unmatched logical lines in the original editor layout.
layout = (ROOT / "sakura_core/doc/layout/CLayoutMgr.cpp").read_text(encoding="utf-8-sig")
layout_header = (ROOT / "sakura_core/doc/layout/CLayoutMgr.h").read_text(encoding="utf-8-sig")
require("SetRealtimeFilterVisibleRanges(displayRanges, true)", "apply matching ranges to the source editor")
require("SetRealtimeFilterVisibleRanges({}, false)", "restore all source lines when clearing or closing")
require("::ShowWindow(m_cSplitterWnd.GetHwnd(), SW_SHOW)", "keep the original editor visible during filtering")
require("if (m_hwndRealtimeFilterList) ::ShowWindow(m_hwndRealtimeFilterList, SW_HIDE)", "do not replace the editor with a separate results list")
require("含む語句", "Japanese include label")
require("除外語句", "Japanese exclude label")
require("すべての語句に一致", "explain AND behavior in the control itself")
require("いずれかの語句に一致", "explain OR behavior in the control itself")
require('const std::wstring stats = L"一致 "', "Japanese match count status")
if "bool CLayoutMgr::IsDisplayHiddenLine(CLogicInt logicLine) const" not in layout:
    raise SystemExit("missing realtime-filter visibility mapping")
if "bool IsDisplayHiddenLine(CLogicInt logicLine) const" not in layout_header:
    raise SystemExit("missing source-view visibility API")
require("case F_FILESAVE:", "allow original document Save while filtered")
require("result->documentVersion != GetDocument()->m_uCurlSyntaxVersion", "reject stale document results")
require("result->requestToken != m_uRealtimeFilterToken", "reject stale request results")

# Explicit cancellation invalidates queued messages and asks the worker to stop.
require("IDC_REALTIME_FILTER_CANCEL", "cancel control")
require("case IDC_REALTIME_FILTER_CANCEL:", "cancel action")
require("id <= IDC_REALTIME_FILTER_CANCEL", "cancel control lifecycle")
require("++m_uRealtimeFilterToken;", "cancel generation invalidation")
require("if (m_pRealtimeFilterService) m_pRealtimeFilterService->Cancel();", "worker cancellation")
require("処理を中止しました。原文全行を表示しています", "cancel restores the original full view")

# The incremental append cache validates old line content and line-ending state.
require("m_bRealtimeFilterAppendCachePending = m_bRealtimeFilterScanComplete", "append cache gate")
require("textfilter::FingerprintLine(lineText, line->GetEol().IsValid())", "line fingerprint check")
require("m_vRealtimeFilterAppendCacheFingerprints[lineNumber]", "detect changed old prefix")
require("m_bRealtimeFilterDocumentLoading", "wait for external reload completion")

# Export is read-only from the source model and uses only visible source ranges.
require("for (const auto& range : m_vRealtimeFilterVisibleRanges)", "visible-range export")
require("::SendMessageCmd(newWindow, WM_COMMAND, MAKEWPARAM(F_PASTE, 0), 0)", "export into identified new window")
require("if (!newWindow || newWindow == GetHwnd())", "avoid pasting into source window")

print("realtime-filter source-view integration static checks passed (not a GUI test)")
