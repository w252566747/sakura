#!/usr/bin/env python3
"""Static integration checks for responsive Win32 CSV snapshot collection."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "sakura_core/window/CEditWnd.cpp").read_text(encoding="utf-8-sig")
header = (ROOT / "sakura_core/window/CEditWnd.h").read_text(encoding="utf-8-sig")


def require(text: str, fragment: str, description: str) -> None:
    if fragment not in text:
        raise SystemExit(f"missing CSV integration: {description}")


require(header, "IDT_CSV_TABLE_SNAPSHOT", "snapshot collection timer")
require(header, "void ContinueCSVTableSnapshot();", "chunk continuation entry point")
require(header, "void SubmitCSVTableParseSnapshot();", "worker submit entry point")
require(source, "case IDT_CSV_TABLE_SNAPSHOT:", "timer dispatch")
require(source, "processedLines < 2048", "bounded per-turn row count")
require(source, "\n\t\t\t\t32768,", "bounded single-line copy size")
require(source, "std::chrono::milliseconds(4)", "bounded per-turn time budget")
require(source, "GetDocument()->m_uCurlSyntaxVersion != m_uCSVTableSnapshotVersion", "restart when document changes")
require(source, "csvtable::snapshot::AppendNextChunk(", "production bounded chunk helper")
chunk_helper = (ROOT / "sakura_core/parse/CSVTableSnapshotChunk.h").read_text(encoding="utf-8")
require(chunk_helper, "sourceOffset += chunkLength", "advance source offset after successful append")
require(chunk_helper, "std::min(remaining, maxChunkLength)", "respect maximum per-call chunk length")
require(source, "m_pCSVTableParseService->Schedule(documentId, version, std::move(text)", "parse snapshot on worker")
require(source, "KillTimer(m_hWnd, IDT_CSV_TABLE_SNAPSHOT)", "stop collection at window teardown")
schedule = source[source.index("void CEditWnd::ScheduleCSVTableParse()"):source.index("void CEditWnd::ContinueCSVTableSnapshot()")]
if "for (const CDocLine* line" in schedule:
    raise SystemExit("CSV scheduling still walks the entire source synchronously")
loading = source[source.index("void CEditWnd::NotifyRealtimeFilterDocumentLoading"):source.index("void CEditWnd::NotifyRealtimeFilterDocumentLoadFinished")]
require(loading, "m_pCSVTableSnapshotScanLine = nullptr", "invalidate line cursor before document replacement")
require(loading, "FinishCSVCellEdit(false)", "discard in-progress cell editor before document replacement")
require(loading, "m_pCSVTableParseService->Cancel()", "cancel parse before document replacement")
finished = source[source.index("void CEditWnd::NotifyRealtimeFilterDocumentLoadFinished"):source.index("void CEditWnd::CloseRealtimeFilter")]
require(finished, "if (m_bCSVTableMode) ScheduleCSVTableParse();", "rebuild CSV snapshot after document load")

print("CSV snapshot UI integration static checks passed (not a Win32 GUI test)")
