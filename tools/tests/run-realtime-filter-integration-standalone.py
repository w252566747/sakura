#!/usr/bin/env python3
"""Static integration checks for the Win32 realtime-filter UI wiring."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "sakura_core/window/CEditWnd.cpp").read_text(encoding="utf-8-sig")


def require(fragment: str, description: str) -> None:
    if fragment not in source:
        raise SystemExit(f"missing realtime-filter integration: {description}")


# Filtering remains a separate owner-data view and does not rewrite document lines.
require("LVS_OWNERDATA", "virtual results list")
require("line->GetPtr(), lineLength", "render directly from source document lines")
require("case F_FILESAVE:", "allow original document Save while filtered")
require("result->documentVersion != GetDocument()->m_uCurlSyntaxVersion", "reject stale document results")
require("result->requestToken != m_uRealtimeFilterToken", "reject stale request results")

# Explicit cancellation invalidates queued messages and asks the worker to stop.
require("IDC_REALTIME_FILTER_CANCEL", "cancel control")
require("case IDC_REALTIME_FILTER_CANCEL:", "cancel action")
require("id <= IDC_REALTIME_FILTER_CANCEL", "cancel control lifecycle")
require("++m_uRealtimeFilterToken;", "cancel generation invalidation")
require("if (m_pRealtimeFilterService) m_pRealtimeFilterService->Cancel();", "worker cancellation")
require("中止しました（部分結果 ", "cancelled partial-results status")

# The incremental append cache validates old line content and line-ending state.
require("m_bRealtimeFilterAppendCachePending = m_bRealtimeFilterScanComplete", "append cache gate")
require("textfilter::FingerprintLine(lineText, line->GetEol().IsValid())", "line fingerprint check")
require("m_vRealtimeFilterAppendCacheFingerprints[lineNumber]", "detect changed old prefix")
require("m_bRealtimeFilterDocumentLoading", "wait for external reload completion")

# Export is read-only from the source model and uses only visible source ranges.
require("for (const auto& range : m_vRealtimeFilterVisibleRanges)", "visible-range export")
require("::SendMessageCmd(newWindow, WM_COMMAND, MAKEWPARAM(F_PASTE, 0), 0)", "export into identified new window")
require("if (!newWindow || newWindow == GetHwnd())", "avoid pasting into source window")

print("realtime-filter Win32 UI wiring static checks passed (not a GUI test)")
