/*!	@file
	@brief 文書ウィンドウの管理

	@author kobake
	@date	2008/04/14 作成
*/
/*
	Copyright (C) 1998-2001, Norio Nakatani
	Copyright (C) 2000-2001, genta, jepro
	Copyright (C) 2001, asa-o, MIK, hor, Misaka, Stonee, YAZAKI
	Copyright (C) 2002, genta, hor, YAZAKI, Azumaiya, KK, novice, minfu, ai, aroka, MIK
	Copyright (C) 2003, genta, MIK, Moca
	Copyright (C) 2004, genta, Moca, novice, Kazika, isearch
	Copyright (C) 2005, genta, Moca, MIK, ryoji, maru
	Copyright (C) 2006, genta, aroka, fon, yukihane, ryoji
	Copyright (C) 2007, ryoji, maru
	Copyright (C) 2008, ryoji
	Copyright (C) 2009, nasukoji
	Copyright (C) 2010, Moca
	Copyright (C) 2012, ryoji, Uchi
	Copyright (C) 2018-2022, Sakura Editor Organization

	This source code is designed for sakura editor.
	Please contact the copyright holder to use this code for other purpose.
*/
#include "StdAfx.h"
#include "CEditView.h"
#include "CRuler.h"
#include "env/DLLSHAREDATA.h"
#include "doc/logic/CDocLine.h"
#include "docplus/CBookmarkManager.h"
#include "docplus/CModifyManager.h"
#include "view/colors/CWordMarker.h"
#include "window/CEditWnd.h"
#include "types/CTypeSupport.h"
#include <limits.h>
#include "config/app_constants.h"
#include "apiwrap/DarkMode.h"
#include <CommCtrl.h>
#include <commdlg.h>
#include <array>
#include <chrono>
#include <map>
#include <string>
#include <utility>
#include <vector>

VOID CALLBACK EditViewTimerProc(HWND, UINT, UINT_PTR, DWORD);

namespace {
constexpr UINT_PTR IDT_SCROLLBAR_MARKERS = 2;
constexpr int SCROLLBAR_MARKER_MENU_BASE = 0x6b00;
constexpr UINT SCROLLBAR_MARKER_KIND_COUNT = 6;
unsigned g_uScrollBarMarkerVisibility = CScrollBarMarkerModel::VISIBILITY_MASK;
std::array<unsigned, SCROLLBAR_MARKER_KIND_COUNT> g_uScrollBarMarkerColors{
	0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };

void InvalidateAllScrollBarMarkers()
{
	CEditWnd* editWindow = CEditWnd::getInstance();
	if (!editWindow) return;
	for (int index = 0; index < editWindow->GetAllViewCount(); ++index) {
		HWND scrollBar = editWindow->GetView(index).m_hwndVScrollBar;
		if (scrollBar) ::InvalidateRect(scrollBar, nullptr, FALSE);
	}
}

const wchar_t* ScrollBarMarkerKindName(CScrollBarMarkerModel::Kind kind)
{
	return CScrollBarMarkerModel::KindName(kind);
}

COLORREF GetTypeColor(const CEditView* view, EColorIndexType index, COLORREF fallback)
{
	if (!view || !view->m_pTypeData || static_cast<int>(index) < 0 || index >= COLORIDX_LAST) return fallback;
	return view->m_pTypeData->m_ColorInfoArr[index].m_sColorAttr.m_cBACK;
}

COLORREF GetWordMarkerColor(const CEditView* view, unsigned colorCode)
{
	if (CWordMarker::IsCustomColor(colorCode)) return static_cast<COLORREF>(CWordMarker::DecodeCustomColor(colorCode));
	const auto& colors = view->m_pTypeData->m_ColorInfoArr;
	if (colorCode > 0 && colorCode <= static_cast<unsigned>(COLORIDX_SEARCHTAIL - COLORIDX_SEARCH)) {
		const int preferred = COLORIDX_SEARCH + static_cast<int>(colorCode);
		if (colors[preferred].m_sColorAttr.m_cTEXT != colors[preferred].m_sColorAttr.m_cBACK) {
			return colors[preferred].m_sColorAttr.m_cBACK;
		}
	}
	for (int index = COLORIDX_SEARCH + 1; index <= COLORIDX_SEARCHTAIL; ++index) {
		if (colors[index].m_sColorAttr.m_cTEXT != colors[index].m_sColorAttr.m_cBACK) return colors[index].m_sColorAttr.m_cBACK;
	}
	return RGB(255, 220, 130);
}

int ScrollBarMarkerTrackTop()
{
	return std::max(0, ::GetSystemMetrics(SM_CYVSCROLL));
}
}

unsigned CEditView::GetScrollBarMarkerVisibility() noexcept
{
	return g_uScrollBarMarkerVisibility;
}

void CEditView::SetScrollBarMarkerVisibility(unsigned mask) noexcept
{
	g_uScrollBarMarkerVisibility = CScrollBarMarkerModel::NormalizeVisibilityMask(mask);
}

unsigned CEditView::GetScrollBarMarkerColor(unsigned kind) noexcept
{
	return kind < g_uScrollBarMarkerColors.size() ? g_uScrollBarMarkerColors[kind] : 0xffffffffu;
}

void CEditView::SetScrollBarMarkerColor(unsigned kind, unsigned color) noexcept
{
	if (kind < g_uScrollBarMarkerColors.size()) {
		g_uScrollBarMarkerColors[kind] = color <= 0x00ffffffu ? color : 0xffffffffu;
	}
}

/*! スクロールバー作成
	@date 2006.12.19 ryoji 新規作成（CEditView::Createから分離）
*/
BOOL CEditView::CreateScrollBar()
{
	SCROLLINFO	si;

	/* スクロールバーの作成 */
	m_hwndVScrollBar = ::CreateWindowEx(
		0L,									/* no extended styles */
		WC_SCROLLBAR,						/* scroll bar control class */
		nullptr,								/* text for window title bar */
		WS_VISIBLE | WS_CHILD | SBS_VERT,	/* scroll bar styles */
		0,									/* horizontal position */
		0,									/* vertical position */
		200,								/* width of the scroll bar */
		CW_USEDEFAULT,						/* default height */
		GetHwnd(),								/* handle of main window */
		(HMENU) nullptr,						/* no menu for a scroll bar */
		G_AppInstance(),						/* instance owning this window */
		(LPVOID) nullptr						/* pointer not needed */
	);
	si.cbSize = sizeof( si );
	si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
	si.nMin  = 0;
	si.nMax  = 29;
	si.nPage = 10;
	si.nPos  = 0;
	si.nTrackPos = 1;
	::SetScrollInfo( m_hwndVScrollBar, SB_CTL, &si, TRUE );
	::ShowScrollBar( m_hwndVScrollBar, SB_CTL, TRUE );
	if (!m_bMiniMap) {
		::SetWindowSubclass(m_hwndVScrollBar, ScrollBarMarkerSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
		m_hwndScrollBarMarkerTooltip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
			WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			GetHwnd(), nullptr, G_AppInstance(), nullptr);
		if (m_hwndScrollBarMarkerTooltip) {
			TOOLINFOW tool{};
			tool.cbSize = sizeof(tool);
			tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
			tool.hwnd = GetHwnd();
			tool.uId = 1;
			tool.lpszText = const_cast<wchar_t*>(L"");
			::SendMessageW(m_hwndScrollBarMarkerTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
		}
		::SetTimer(GetHwnd(), IDT_SCROLLBAR_MARKERS, 100, EditViewTimerProc);
	}

	/* スクロールバーの作成 */
	m_hwndHScrollBar = nullptr;
	if( GetDllShareData().m_Common.m_sWindow.m_bScrollBarHorz && !m_bMiniMap ){	/* 水平スクロールバーを使う */
		m_hwndHScrollBar = ::CreateWindowEx(
			0L,									/* no extended styles */
			WC_SCROLLBAR,						/* scroll bar control class */
			nullptr,								/* text for window title bar */
			WS_VISIBLE | WS_CHILD | SBS_HORZ,	/* scroll bar styles */
			0,									/* horizontal position */
			0,									/* vertical position */
			200,								/* width of the scroll bar */
			CW_USEDEFAULT,						/* default height */
			GetHwnd(),								/* handle of main window */
			(HMENU) nullptr,						/* no menu for a scroll bar */
			G_AppInstance(),						/* instance owning this window */
			(LPVOID) nullptr						/* pointer not needed */
		);
		si.cbSize = sizeof( si );
		si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
		si.nMin  = 0;
		si.nMax  = 29;
		si.nPage = 10;
		si.nPos  = 0;
		si.nTrackPos = 1;
		::SetScrollInfo( m_hwndHScrollBar, SB_CTL, &si, TRUE );
		::ShowScrollBar( m_hwndHScrollBar, SB_CTL, TRUE );
	}

	/* サイズボックス */
	if( GetDllShareData().m_Common.m_sWindow.m_nFUNCKEYWND_Place == 0 ){	/* ファンクションキー表示位置／0:上 1:下 */
		::ShowWindow( m_hwndSizeBoxPlaceholder, SW_HIDE );
		::ShowWindow( m_hwndSizeBox, SW_SHOW );
	}else{
		::ShowWindow( m_hwndSizeBox, SW_HIDE );
		::ShowWindow( m_hwndSizeBoxPlaceholder, SW_SHOW );
	}

	DarkMode::setChildCtrlsTheme(GetHwnd());

	return TRUE;
}

/*! スクロールバー破棄
	@date 2006.12.19 ryoji 新規作成
*/
void CEditView::DestroyScrollBar()
{
	::KillTimer(GetHwnd(), IDT_SCROLLBAR_MARKERS);
	if (m_hwndScrollBarMarkerTooltip) {
		::DestroyWindow(m_hwndScrollBarMarkerTooltip);
		m_hwndScrollBarMarkerTooltip = nullptr;
	}
	if( m_hwndVScrollBar )
	{
		::RemoveWindowSubclass(m_hwndVScrollBar, ScrollBarMarkerSubclassProc, 1);
		::DestroyWindow( m_hwndVScrollBar );
		m_hwndVScrollBar = nullptr;
	}

	if( m_hwndHScrollBar )
	{
		::DestroyWindow( m_hwndHScrollBar );
		m_hwndHScrollBar = nullptr;
	}

	::ShowWindow( m_hwndSizeBox, SW_HIDE );
	::ShowWindow( m_hwndSizeBoxPlaceholder, SW_HIDE );
}

void CEditView::ResetScrollBarMarkerScan()
{
	m_bScrollBarMarkerCacheKeyValid = false;
	m_bScrollBarMarkerScanActive = false;
	m_pScrollBarMarkerScanLine = nullptr;
	m_nScrollBarMarkerScanLine = 0;
	m_nScrollBarMarkerScanOffset = 0;
	m_nScrollBarMarkerSeenColorCount = 0;
	m_bScrollBarMarkerLinePrepared = false;
	m_nScrollBarMarkerScanTrackHeight = 0;
	m_cScrollBarMarkerModel.ResetOverview(0, 0);
	if (m_hwndVScrollBar) ::InvalidateRect(m_hwndVScrollBar, nullptr, FALSE);
}

void CEditView::UpdateScrollBarMarkerScan()
{
	if (m_bMiniMap || !m_hwndVScrollBar || !::IsWindowVisible(GetHwnd()) || !m_pcEditDoc) return;

	RECT client{};
	if (!::GetClientRect(m_hwndVScrollBar, &client)) return;
	const int trackTop = ScrollBarMarkerTrackTop();
	const int trackHeight = static_cast<int>(std::max<LONG>(0L, client.bottom - trackTop * 2L));
	const int lineCount = std::max(1, static_cast<int>(m_pcEditDoc->m_cLayoutMgr.GetLineCount()) + 2);

	const auto& markerService = CWordMarker::Instance();
	uint64_t key = 1469598103934665603ull;
	auto mix = [&key](uint64_t value) { key ^= value; key *= 1099511628211ull; };
	mix(m_pcEditDoc->m_uCurlSyntaxVersion);
	mix(markerService.Generation());
	mix(GetBookmarkGeneration());
	mix(static_cast<uint64_t>(lineCount));
	mix(static_cast<uint64_t>(trackHeight));
	mix(static_cast<uint64_t>(m_bCurSrchKeyMark));
	mix(static_cast<uint64_t>(m_bCurSearchUpdate));
	mix(static_cast<uint64_t>(static_cast<uint32_t>(m_nCurSearchKeySequence)));
	mix(static_cast<uint64_t>(m_sCurSearchOption.bLoHiCase));
	mix(static_cast<uint64_t>(m_sCurSearchOption.bRegularExp));
	mix(static_cast<uint64_t>(m_sCurSearchOption.bWordOnly));
	for (const wchar_t ch : m_strCurSearchKey) mix(static_cast<uint64_t>(ch));
	mix(static_cast<uint64_t>(m_pcEditDoc->m_bCurlSyntaxResultStale));
	mix(static_cast<uint64_t>(m_pcEditDoc->m_vCurlSyntaxDiagnostics.size()));
	mix(static_cast<uint64_t>(m_pcEditDoc->m_cDocEditor.m_cOpeBuf.GetNoModifiedSeq()));
	for (unsigned kind = 0; kind < SCROLLBAR_MARKER_KIND_COUNT; ++kind)
		mix(static_cast<uint64_t>(GetScrollBarMarkerColor(kind)));
	if (!m_bScrollBarMarkerCacheKeyValid || key != m_uScrollBarMarkerScanKey) {
		m_cScrollBarMarkerModel.ResetOverview(trackHeight, lineCount);
		m_uScrollBarMarkerScanKey = key;
		m_uScrollBarMarkerScanVersion = m_pcEditDoc->m_uCurlSyntaxVersion;
		m_nScrollBarMarkerScanTrackHeight = trackHeight;
		m_nScrollBarMarkerScanLine = 0;
		m_nScrollBarMarkerScanOffset = 0;
		m_nScrollBarMarkerSeenColorCount = 0;
		m_bScrollBarMarkerLinePrepared = false;
		m_pScrollBarMarkerScanLine = m_pcEditDoc->m_cDocLineMgr.GetDocLineTop();
		m_bScrollBarMarkerScanActive = m_pScrollBarMarkerScanLine != nullptr;
		m_bScrollBarMarkerCacheKeyValid = true;
		::InvalidateRect(m_hwndVScrollBar, nullptr, FALSE);
	}
	if (!m_bScrollBarMarkerScanActive || m_bCurSearchUpdate) return;
	// A line pointer may be freed by editing between timer callbacks. Version
	// changes are checked before that pointer is dereferenced.
	if (m_uScrollBarMarkerScanVersion != m_pcEditDoc->m_uCurlSyntaxVersion) {
		ResetScrollBarMarkerScan();
		return;
	}

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(4);
	int processed = 0;
	bool addedAny = false;
	const auto* docLine = m_pScrollBarMarkerScanLine;
	auto& layout = m_pcEditDoc->m_cLayoutMgr;
	const auto& diagnostics = m_pcEditDoc->m_vCurlSyntaxDiagnostics;
	const bool syntaxActive = !m_pcEditDoc->m_bCurlSyntaxResultStale && GetEditWnd().IsCurlSyntaxDocument();
	const bool searchActive = m_bCurSrchKeyMark && m_sSearchPattern.GetLen() > 0;
	const bool modifiedActive = m_pcEditDoc->m_cDocEditor.IsModified();
	const int saveSequence = m_pcEditDoc->m_cDocEditor.m_cOpeBuf.GetNoModifiedSeq();

	while (docLine && processed < 256 && std::chrono::steady_clock::now() < deadline) {
		const int physicalLine = m_nScrollBarMarkerScanLine;
		const wchar_t* text = docLine->GetPtr();
		const int length = static_cast<int>(docLine->GetLengthWithoutEOL());
		bool mapped = false;
		int overviewLine = 0;
		auto mapLine = [&]() {
			if (!mapped) {
				CLayoutPoint point;
				layout.LogicToLayout(CLogicPoint(CLogicInt(0), CLogicInt(physicalLine)), &point, CLayoutInt(0), false);
				overviewLine = static_cast<int>(point.GetY());
				mapped = true;
			}
			return overviewLine;
		};
		auto addMarker = [&](CScrollBarMarkerModel::Kind kind, COLORREF color, const std::wstring& label, const std::wstring& detail) {
			const unsigned customColor = GetScrollBarMarkerColor(static_cast<unsigned>(kind));
			if (kind != CScrollBarMarkerModel::Kind::WordMarker && customColor != 0xffffffffu)
				color = static_cast<COLORREF>(customColor);
			CScrollBarMarkerModel::Marker marker;
			marker.line = mapLine();
			marker.logicalLine = physicalLine;
			marker.kind = kind;
			marker.color = static_cast<std::uint32_t>(color);
			marker.label = label;
			marker.detail = detail;
			m_cScrollBarMarkerModel.AddOverviewMarker(marker);
			addedAny = true;
		};
		const std::wstring lineDetail = L"行 " + std::to_wstring(physicalLine + 1);

		if (!m_bScrollBarMarkerLinePrepared) {
			// Avoid running a potentially expensive regex across a single unusually
			// long line on the UI thread. Normal lines use Sakura's own search rules.
			if (searchActive && length > 0 && length <= 65536) {
				CLogicInt matchStart, matchEnd;
				if (IsSearchString(CStringRef(text, static_cast<std::size_t>(length)), CLogicInt(0), &matchStart, &matchEnd)) {
					addMarker(CScrollBarMarkerModel::Kind::Search, GetTypeColor(this, COLORIDX_SEARCH, RGB(255, 220, 0)),
						L"検索", m_strCurSearchKey + L" / " + lineDetail);
				}
			}
			if (syntaxActive) {
				const int oneBasedLine = physicalLine + 1;
				const auto first = std::lower_bound(diagnostics.begin(), diagnostics.end(), oneBasedLine,
					[](const curlsyntax::Diagnostic& item, int value) { return item.line < value; });
				for (auto it = first; it != diagnostics.end() && it->line == oneBasedLine; ++it) {
					const bool isError = it->severity == curlsyntax::Severity::Error;
					std::wstring detail;
					if (!it->code.empty()) detail.assign(it->code.begin(), it->code.end());
					if (!it->message.empty()) {
						if (!detail.empty()) detail += L": ";
						detail += it->message;
					}
					if (!detail.empty()) detail += L" / ";
					detail += lineDetail;
					addMarker(isError ? CScrollBarMarkerModel::Kind::SyntaxError : CScrollBarMarkerModel::Kind::SyntaxWarning,
						isError ? RGB(210, 55, 55) : RGB(225, 155, 35), isError ? L"Curl Error" : L"Curl Warning",
						std::move(detail));
				}
			}
			if (CBookmarkGetter(docLine).IsBookmarked()) {
				addMarker(CScrollBarMarkerModel::Kind::Bookmark, GetTypeColor(this, COLORIDX_MARK, RGB(80, 130, 220)), L"ブックマーク", lineDetail);
			}
			if (modifiedActive && CModifyVisitor().IsLineModified(docLine, saveSequence)) {
				addMarker(CScrollBarMarkerModel::Kind::Modified, GetTypeColor(this, COLORIDX_GYOU_MOD, RGB(45, 150, 90)), L"変更行", lineDetail);
			}
			m_bScrollBarMarkerLinePrepared = true;
		}

		const int chunkEnd = m_nScrollBarMarkerScanOffset + std::min(2048, length - m_nScrollBarMarkerScanOffset);
		while (m_nScrollBarMarkerScanOffset < chunkEnd) {
			int end = m_nScrollBarMarkerScanOffset;
			const int match = markerService.Match(m_pcEditDoc, text, length, m_nScrollBarMarkerScanOffset, &end);
			if (match <= 0 || end <= m_nScrollBarMarkerScanOffset) {
				++m_nScrollBarMarkerScanOffset;
				continue;
			}
			const unsigned colorCode = match == 33 ? 0x00c8a46b : markerService.ColorAt(match);
			if (std::find(m_aScrollBarMarkerSeenColors.begin(),
				m_aScrollBarMarkerSeenColors.begin() + m_nScrollBarMarkerSeenColorCount, colorCode) ==
				m_aScrollBarMarkerSeenColors.begin() + m_nScrollBarMarkerSeenColorCount) {
				m_aScrollBarMarkerSeenColors[m_nScrollBarMarkerSeenColorCount++] = colorCode;
				std::wstring word = match == 33
					? (markerService.AutomaticWord(m_pcEditDoc) ? *markerService.AutomaticWord(m_pcEditDoc) : L"自動")
					: markerService.Entries()[static_cast<std::size_t>(match - 1)].word;
				addMarker(CScrollBarMarkerModel::Kind::WordMarker, match == 33
					? GetTypeColor(this, COLORIDX_DIFF_DELETE, RGB(255, 205, 150))
					: GetWordMarkerColor(this, colorCode), word, lineDetail);
			}
			m_nScrollBarMarkerScanOffset = end;
		}

		if (m_nScrollBarMarkerScanOffset < length) continue;
		docLine = docLine->GetNextLine();
		++m_nScrollBarMarkerScanLine;
		++processed;
		m_nScrollBarMarkerScanOffset = 0;
		m_nScrollBarMarkerSeenColorCount = 0;
		m_bScrollBarMarkerLinePrepared = false;
	}
	m_pScrollBarMarkerScanLine = docLine;
	if (!docLine) m_bScrollBarMarkerScanActive = false;
	if (addedAny) ::InvalidateRect(m_hwndVScrollBar, nullptr, FALSE);
}

void CEditView::DrawScrollBarMarkers(HDC hdc)
{
	if (!hdc || m_bMiniMap || !m_hwndVScrollBar || m_nScrollBarMarkerScanTrackHeight <= 0) return;
	RECT client{};
	if (!::GetClientRect(m_hwndVScrollBar, &client)) return;
	const int width = client.right - client.left;
	const int trackTop = ScrollBarMarkerTrackTop();
	const int trackHeight = static_cast<int>(std::max<LONG>(0L, client.bottom - trackTop * 2L));
	if (trackHeight <= 0 || width < 6) return;

	int thumbTop = -1;
	int thumbBottom = -1;
	SCROLLINFO info{};
	info.cbSize = sizeof(info);
	info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	if (::GetScrollInfo(m_hwndVScrollBar, SB_CTL, &info)) {
		const int range = std::max(1, info.nMax - info.nMin + 1);
		const int thumbHeight = std::min(trackHeight, std::max(8, ::MulDiv(trackHeight, static_cast<int>(info.nPage), range)));
		const int maxPosition = std::max(info.nMin, info.nMax - static_cast<int>(info.nPage) + 1);
		const int travel = std::max(0, trackHeight - thumbHeight);
		const int position = std::clamp(info.nPos, info.nMin, maxPosition);
		thumbTop = trackTop + (maxPosition == info.nMin ? 0 : ::MulDiv(position - info.nMin, travel, maxPosition - info.nMin));
		thumbBottom = thumbTop + thumbHeight;
	}

	const int laneWidth = 2;
	const int laneCount = static_cast<int>(SCROLLBAR_MARKER_KIND_COUNT);
	const int markerLeft = std::max(0, width - laneWidth * laneCount - 2);
	const auto& ticks = m_cScrollBarMarkerModel.OverviewTicks();
	std::map<COLORREF, HBRUSH> brushes;
	const int saved = ::SaveDC(hdc);
	::IntersectClipRect(hdc, 0, trackTop, width, trackTop + trackHeight);
	for (const auto& tick : ticks) {
		if (tick.lane < 0 || tick.lane >= laneCount || !(GetScrollBarMarkerVisibility() & (1u << tick.lane))) continue;
		const int y = trackTop + tick.y;
		if (thumbTop >= 0 && y >= thumbTop - 1 && y <= thumbBottom) continue;
		auto found = brushes.find(static_cast<COLORREF>(tick.color));
		if (found == brushes.end()) found = brushes.emplace(static_cast<COLORREF>(tick.color), ::CreateSolidBrush(static_cast<COLORREF>(tick.color))).first;
		if (found->second) {
			RECT markerRect{ markerLeft + tick.lane * laneWidth, y, markerLeft + (tick.lane + 1) * laneWidth, y + 3 };
			::FillRect(hdc, &markerRect, found->second);
		}
	}
	if (saved) ::RestoreDC(hdc, saved);
	for (const auto& brush : brushes) if (brush.second) ::DeleteObject(brush.second);
}

bool CEditView::NavigateScrollBarMarker(int x, int y)
{
	if (!m_hwndVScrollBar || !m_bScrollBarMarkerCacheKeyValid) return false;
	RECT client{};
	::GetClientRect(m_hwndVScrollBar, &client);
	const int trackTop = ScrollBarMarkerTrackTop();
	const int trackHeight = static_cast<int>(std::max<LONG>(0L, client.bottom - trackTop * 2L));
	const int markerLeft = static_cast<int>(std::max<LONG>(0L, client.right - 14L));
	const int laneWidth = 2;
	if (y < trackTop || y >= trackTop + trackHeight || x < markerLeft) return false;
	const int lane = (x - markerLeft) / laneWidth;
	const int localY = y - trackTop;
	const auto& ticks = m_cScrollBarMarkerModel.OverviewTicks();
	auto found = lane >= 0 && lane < static_cast<int>(SCROLLBAR_MARKER_KIND_COUNT)
		? CScrollBarMarkerModel::HitTestLane(ticks, lane, localY, 3)
		: CScrollBarMarkerModel::HitTest(ticks, localY, 3);
	if (found == ticks.size() || !(GetScrollBarMarkerVisibility() & (1u << ticks[found].lane))) return false;
	const auto& tick = ticks[found];
	if (m_pcEditDoc && tick.firstLogicalLine >= 0
		&& CLogicInt(tick.firstLogicalLine) < m_pcEditDoc->m_cDocLineMgr.GetLineCount()) {
		CLayoutPoint target;
		m_pcEditDoc->m_cLayoutMgr.LogicToLayout(
			CLogicPoint(CLogicInt(0), CLogicInt(tick.firstLogicalLine)), &target);
		ScrollAtV(target.GetY2());
	} else {
		ScrollAtV(CLayoutInt(tick.firstLine));
	}
	return true;
}

void CEditView::UpdateScrollBarMarkerTooltip(int x, int y)
{
	if (!m_hwndScrollBarMarkerTooltip || !m_hwndVScrollBar || !m_bScrollBarMarkerCacheKeyValid) return;
	RECT client{};
	::GetClientRect(m_hwndVScrollBar, &client);
	const int trackTop = ScrollBarMarkerTrackTop();
	const int trackHeight = static_cast<int>(std::max<LONG>(0L, client.bottom - trackTop * 2L));
	const int markerLeft = static_cast<int>(std::max<LONG>(0L, client.right - 14L));
	if (y < trackTop || y >= trackTop + trackHeight || x < markerLeft) {
		TOOLINFOW tool{};
		tool.cbSize = sizeof(tool);
		tool.hwnd = GetHwnd();
		tool.uId = 1;
		::SendMessageW(m_hwndScrollBarMarkerTooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
		return;
	}
	const int lane = (x - markerLeft) / 2;
	const auto& ticks = m_cScrollBarMarkerModel.OverviewTicks();
	const auto hit = lane >= 0 && lane < static_cast<int>(SCROLLBAR_MARKER_KIND_COUNT)
		? CScrollBarMarkerModel::HitTestLane(ticks, lane, y - trackTop, 3)
		: CScrollBarMarkerModel::HitTest(ticks, y - trackTop, 3);
	if (hit == ticks.size() || !(GetScrollBarMarkerVisibility() & (1u << ticks[hit].lane))) {
		TOOLINFOW tool{};
		tool.cbSize = sizeof(tool);
		tool.hwnd = GetHwnd();
		tool.uId = 1;
		::SendMessageW(m_hwndScrollBarMarkerTooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
		return;
	}
	const auto& tick = ticks[hit];
	m_strScrollBarMarkerTooltip = CScrollBarMarkerModel::FormatTooltip(tick);
	TOOLINFOW tool{};
	tool.cbSize = sizeof(tool);
	tool.hwnd = GetHwnd();
	tool.uId = 1;
	tool.lpszText = const_cast<wchar_t*>(m_strScrollBarMarkerTooltip.c_str());
	::SendMessageW(m_hwndScrollBarMarkerTooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
	POINT screen{ x, y };
	::ClientToScreen(m_hwndVScrollBar, &screen);
	::SendMessageW(m_hwndScrollBarMarkerTooltip, TTM_TRACKPOSITION, 0,
		MAKELONG(screen.x + DpiScaleX(12), screen.y + DpiScaleY(18)));
	::SendMessageW(m_hwndScrollBarMarkerTooltip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
}

LRESULT CALLBACK CEditView::ScrollBarMarkerSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData)
{
	auto* view = reinterpret_cast<CEditView*>(refData);
	if (!view) return ::DefSubclassProc(hwnd, message, wParam, lParam);
	switch (message) {
	case WM_PAINT: {
		const LRESULT result = ::DefSubclassProc(hwnd, message, wParam, lParam);
		HDC hdc = ::GetDC(hwnd);
		if (hdc) {
			view->DrawScrollBarMarkers(hdc);
			::ReleaseDC(hwnd, hdc);
		}
		return result;
	}
	case WM_LBUTTONDOWN:
		if (view->NavigateScrollBarMarker(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) return 0;
		break;
	case WM_MOUSEMOVE:
		view->UpdateScrollBarMarkerTooltip(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		{
			TRACKMOUSEEVENT tracking{};
			tracking.cbSize = sizeof(tracking);
			tracking.dwFlags = TME_LEAVE;
			tracking.hwndTrack = hwnd;
			::TrackMouseEvent(&tracking);
		}
		break;
	case WM_RBUTTONUP: {
		HMENU menu = ::CreatePopupMenu();
		HMENU colorsMenu = ::CreatePopupMenu();
		if (menu) {
			const unsigned visibility = CEditView::GetScrollBarMarkerVisibility();
			for (UINT kind = 0; kind < SCROLLBAR_MARKER_KIND_COUNT; ++kind) {
				const auto markerKind = static_cast<CScrollBarMarkerModel::Kind>(kind);
				::AppendMenuW(menu, MF_STRING | ((visibility & (1u << kind)) ? MF_CHECKED : 0),
					SCROLLBAR_MARKER_MENU_BASE + kind, ScrollBarMarkerKindName(markerKind));
			}
			::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			::AppendMenuW(menu, MF_STRING, SCROLLBAR_MARKER_MENU_BASE + 20, L"すべて表示");
			::AppendMenuW(menu, MF_STRING, SCROLLBAR_MARKER_MENU_BASE + 21, L"すべて非表示");
			if (colorsMenu) {
				static const wchar_t* colorNames[] = { L"検索", L"", L"構文エラー", L"構文警告", L"ブックマーク", L"変更行" };
				for (UINT kind = 0; kind < SCROLLBAR_MARKER_KIND_COUNT; ++kind) {
					if (kind == static_cast<UINT>(CScrollBarMarkerModel::Kind::WordMarker)) continue;
					::AppendMenuW(colorsMenu, MF_STRING, SCROLLBAR_MARKER_MENU_BASE + 30 + kind,
						(std::wstring(colorNames[kind]) + L"...").c_str());
				}
				::AppendMenuW(colorsMenu, MF_SEPARATOR, 0, nullptr);
				::AppendMenuW(colorsMenu, MF_STRING, SCROLLBAR_MARKER_MENU_BASE + 36, L"既定色に戻す");
				::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(colorsMenu), L"色を設定");
			}
			POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			::ClientToScreen(hwnd, &point);
			const UINT command = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, view->GetHwnd(), nullptr);
			::DestroyMenu(menu);
			if (command >= SCROLLBAR_MARKER_MENU_BASE && command < SCROLLBAR_MARKER_MENU_BASE + SCROLLBAR_MARKER_KIND_COUNT) {
				CEditView::SetScrollBarMarkerVisibility(visibility ^ (1u << (command - SCROLLBAR_MARKER_MENU_BASE)));
			} else if (command == SCROLLBAR_MARKER_MENU_BASE + 20) {
				CEditView::SetScrollBarMarkerVisibility(CScrollBarMarkerModel::VISIBILITY_MASK);
			} else if (command == SCROLLBAR_MARKER_MENU_BASE + 21) {
				CEditView::SetScrollBarMarkerVisibility(0);
			} else if (command >= SCROLLBAR_MARKER_MENU_BASE + 30 && command < SCROLLBAR_MARKER_MENU_BASE + 36) {
				const unsigned kind = command - SCROLLBAR_MARKER_MENU_BASE - 30;
				COLORREF custom = CEditView::GetScrollBarMarkerColor(kind);
				if (custom == 0xffffffffu) custom = RGB(255, 220, 0);
				CHOOSECOLORW dialog{};
				dialog.lStructSize = sizeof(dialog);
				dialog.hwndOwner = view->GetHwnd();
				dialog.rgbResult = custom;
				COLORREF customColors[16]{};
				dialog.lpCustColors = customColors;
				dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
				if (::ChooseColorW(&dialog)) CEditView::SetScrollBarMarkerColor(kind, dialog.rgbResult);
			} else if (command == SCROLLBAR_MARKER_MENU_BASE + 36) {
				for (unsigned kind = 0; kind < SCROLLBAR_MARKER_KIND_COUNT; ++kind)
					CEditView::SetScrollBarMarkerColor(kind, 0xffffffffu);
			} else {
				break;
			}
			InvalidateAllScrollBarMarkers();
		}
		return 0;
	}
	case WM_MOUSELEAVE:
		view->UpdateScrollBarMarkerTooltip(-1, -1);
		break;
	case WM_NCDESTROY:
		::RemoveWindowSubclass(hwnd, ScrollBarMarkerSubclassProc, subclassId);
		break;
	default:
		break;
	}
	return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

/*! 垂直スクロールバーメッセージ処理

	@param nScrollCode [in]	スクロール種別 (Windowsから渡されるもの)
	@param nPos [in]		スクロール位置(THUMBTRACK用)
	@retval	実際にスクロールした行数

	@date 2004.09.11 genta スクロール行数を返すように．
		未使用のhwndScrollBar引数削除．
*/
CLayoutInt CEditView::OnVScroll( int nScrollCode, int nPos )
{
	CLayoutInt nScrollVal = CLayoutInt(0);

	// nPos 32bit対応
	if( nScrollCode == SB_THUMBTRACK || nScrollCode == SB_THUMBPOSITION ){
		if( m_hwndVScrollBar ){
			HWND hWndScroll = m_hwndVScrollBar;
			SCROLLINFO info;
			info.cbSize = sizeof(SCROLLINFO);
			info.fMask = SIF_TRACKPOS;
			::GetScrollInfo(hWndScroll, SB_CTL, &info);
			nPos = info.nTrackPos;
		}
	}

	switch( nScrollCode ){
	case SB_LINEDOWN:
//		for( i = 0; i < 4; ++i ){
//			ScrollAtV( GetTextArea().GetViewTopLine() + 1 );
//		}
		nScrollVal = ScrollAtV( GetTextArea().GetViewTopLine() + GetDllShareData().m_Common.m_sGeneral.m_nRepeatedScrollLineNum );
		break;
	case SB_LINEUP:
//		for( i = 0; i < 4; ++i ){
//			ScrollAtV( GetTextArea().GetViewTopLine() - 1 );
//		}
		nScrollVal = ScrollAtV( GetTextArea().GetViewTopLine() - GetDllShareData().m_Common.m_sGeneral.m_nRepeatedScrollLineNum );
		break;
	case SB_PAGEDOWN:
		nScrollVal = ScrollAtV( GetTextArea().GetBottomLine() );
		break;
	case SB_PAGEUP:
		nScrollVal = ScrollAtV( GetTextArea().GetViewTopLine() - GetTextArea().m_nViewRowNum );
		break;
	case SB_THUMBPOSITION:
		nScrollVal = ScrollAtV( CLayoutInt(nPos), FALSE );
		break;
	case SB_THUMBTRACK:
		nScrollVal = ScrollAtV( CLayoutInt(nPos), FALSE );
		break;
	case SB_TOP:
		nScrollVal = ScrollAtV( CLayoutInt(0) );
		break;
	case SB_BOTTOM:
		nScrollVal = ScrollAtV(( m_pcEditDoc->m_cLayoutMgr.GetLineCount() ) - GetTextArea().m_nViewRowNum );
		break;
	default:
		break;
	}
	return nScrollVal;
}

/*! 水平スクロールバーメッセージ処理

	@param nScrollCode [in]	スクロール種別 (Windowsから渡されるもの)
	@param nPos [in]		スクロール位置(THUMBTRACK用)
	@retval	実際にスクロールした桁数

	@date 2004.09.11 genta スクロール桁数を返すように．
		未使用のhwndScrollBar引数削除．
*/
CLayoutInt CEditView::OnHScroll( int nScrollCode, int nPos )
{
	const CLayoutInt nHScrollNum = GetTextMetrics().GetLayoutXDefault(CKetaXInt(4));
	CLayoutInt nScrollVal = CLayoutInt(0);

	// nPos 32bit対応
	if( nScrollCode == SB_THUMBTRACK || nScrollCode == SB_THUMBPOSITION ){
		if( m_hwndHScrollBar ){
			HWND hWndScroll = m_hwndHScrollBar;
			SCROLLINFO info;
			info.cbSize = sizeof(SCROLLINFO);
			info.fMask = SIF_TRACKPOS;
			::GetScrollInfo(hWndScroll, SB_CTL, &info);
			nPos = info.nTrackPos;
		}
	}

	GetRuler().SetRedrawFlag(); // YAZAKI
	switch( nScrollCode ){
	case SB_LINELEFT:
		nScrollVal = ScrollAtH( GetTextArea().GetViewLeftCol() - nHScrollNum );
		break;
	case SB_LINERIGHT:
		nScrollVal = ScrollAtH( GetTextArea().GetViewLeftCol() + nHScrollNum );
		break;
	case SB_PAGELEFT:
		nScrollVal = ScrollAtH( GetTextArea().GetViewLeftCol() - GetTextArea().m_nViewColNum );
		break;
	case SB_PAGERIGHT:
		nScrollVal = ScrollAtH( GetTextArea().GetRightCol() );
		break;
	case SB_THUMBPOSITION:
		nScrollVal = ScrollAtH( CLayoutInt(nPos), FALSE );
//		MYTRACE( L"nPos=%d\n", nPos );
		break;
	case SB_THUMBTRACK:
		nScrollVal = ScrollAtH( CLayoutInt(nPos), FALSE );
//		MYTRACE( L"nPos=%d\n", nPos );
		break;
	case SB_LEFT:
		nScrollVal = ScrollAtH( CLayoutInt(0) );
		break;
	case SB_RIGHT:
		//	Aug. 14, 2005 genta 折り返し幅をLayoutMgrから取得するように
		nScrollVal = ScrollAtH( m_pcEditDoc->m_cLayoutMgr.GetMaxLineKetas() - GetTextArea().m_nViewColNum );
		break;
	default:
		break;
	}
	return nScrollVal;
}

static void setScrollInfoIfNeeded(HWND hWndScrollBar, int nMax, UINT nPage, int nPos, BOOL bRedraw)
{
	SCROLLINFO siPrev;
	siPrev.cbSize = sizeof(siPrev);
	siPrev.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
	if (!::GetScrollInfo( hWndScrollBar, SB_CTL, &siPrev )) {
		return;
	}
	// nPage と nPos を範囲内に収める
	// https://docs.microsoft.com/en-ca/windows/desktop/api/winuser/nf-winuser-setscrollinfo#remarks
	constexpr int nMin = 0;
	const UINT nPageMax = (UINT)(nMax - nMin + 1);
	if (nPage > nPageMax) {
		nPage = nPageMax;
	}
	if (nPos < nMin) {
		nPos = nMin;
	}
	if (nPos > nMax) {
		nPos = nMax;
	}
	if (siPrev.nMin != 0
		|| siPrev.nMax != nMax
		|| siPrev.nPage != nPage
		|| siPrev.nPos != nPos)
	{
		SCROLLINFO si;
		si.cbSize = sizeof( si );
		si.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
		si.nMin  = 0;
		si.nMax  = nMax;
		si.nPage = nPage;
		si.nPos  = nPos;
		::SetScrollInfo( hWndScrollBar, SB_CTL, &si, bRedraw );
	}
}

/** スクロールバーの状態を更新する

	タブバーのタブ切替時は SIF_DISABLENOSCROLL フラグでの有効化／無効化が正常に動作しない
	（不可視でサイズ変更していることによる影響か？）ので SIF_DISABLENOSCROLL で有効／無効
	の切替に失敗した場合には強制切替する

	@date 2008.05.24 ryoji 有効／無効の強制切替を追加
	@date 2008.06.08 ryoji 水平スクロール範囲にぶら下げ余白を追加
	@date 2009.08.28 nasukoji	「折り返さない」選択時のスクロールバー調整
*/
void CEditView::AdjustScrollBars( BOOL bRedraw )
{
	if( !GetDrawSwitch() ){
		return;
	}

	bool bEnable;

	if( nullptr != m_hwndVScrollBar ){
		/* 垂直スクロールバー */
		const CLayoutInt	nEofMargin = CLayoutInt(2); // EOFとその下のマージン
		const CLayoutInt	nAllLines = m_pcEditDoc->m_cLayoutMgr.GetLineCount() + nEofMargin;
		int	nVScrollRate = 1;
#ifdef _WIN64
		/* nAllLines / nVScrollRate < INT_MAX となる整数nVScrollRateを求める */
		// 64bit版用スクロール率
		while( nAllLines / nVScrollRate > INT_MAX ){
			++nVScrollRate;
		}
#endif

		setScrollInfoIfNeeded(m_hwndVScrollBar,
							  (Int)nAllLines / nVScrollRate - 1,					/* 全行数 */
							  (Int)GetTextArea().m_nViewRowNum / nVScrollRate,		/* 表示域の行数 */
							  (Int)GetTextArea().GetViewTopLine() / nVScrollRate,	/* 表示域の一番上の行(0開始) */
							  bRedraw
		);
		m_nVScrollRate = nVScrollRate;				/* 垂直スクロールバーの縮尺 */
		
		//	Nov. 16, 2002 genta
		//	縦スクロールバーがDisableになったときは必ず全体が画面内に収まるように
		//	スクロールさせる
		//	2005.11.01 aroka 判定条件誤り修正 (バーが消えてもスクロールしない)
		bEnable = ( GetTextArea().m_nViewRowNum < nAllLines );
		if( bEnable != (::IsWindowEnabled( m_hwndVScrollBar ) != 0) ){
			::EnableWindow( m_hwndVScrollBar, bEnable? TRUE: FALSE );	// SIF_DISABLENOSCROLL 誤動作時の強制切替
		}
		if( !bEnable ){
			ScrollAtV( CLayoutInt(0) );
		}
	}
	if( nullptr != m_hwndHScrollBar ){
		/* 水平スクロールバー */
		setScrollInfoIfNeeded(m_hwndHScrollBar,
							  (Int)GetRightEdgeForScrollBar() - 1,		// 2009.08.28 nasukoji	スクロールバー制御用の右端座標を取得
							  (Int)GetTextArea().m_nViewColNum,			/* 表示域の桁数 */
							  (Int)GetTextArea().GetViewLeftCol(),		/* 表示域の一番左の桁(0開始) */
							  bRedraw
		);

		//	2006.1.28 aroka 判定条件誤り修正 (バーが消えてもスクロールしない)
		bEnable = ( GetTextArea().m_nViewColNum < GetRightEdgeForScrollBar() );
		if( bEnable != (::IsWindowEnabled( m_hwndHScrollBar ) != 0) ){
			::EnableWindow( m_hwndHScrollBar, bEnable? TRUE: FALSE );	// SIF_DISABLENOSCROLL 誤動作時の強制切替
		}
		if( !bEnable ){
			ScrollAtH( CLayoutInt(0) );
		}
	}
}

/*! 指定上端行位置へスクロール

	@param nPos [in] スクロール位置
	@param bRedrawScrollBar [in] スクロールバーを再描画するかどうか
	@retval 実際にスクロールした行数 (正:下方向/負:上方向)

	@date 2004.09.11 genta 行数を戻り値として返すように．(同期スクロール用)
*/
CLayoutInt CEditView::ScrollAtV( CLayoutInt nPos, BOOL bRedrawScrollBar )
{
	CLayoutInt	nScrollRowNum;
	RECT		rcScrol;
	RECT		rcClip;
	if( nPos < 0 ){
		nPos = CLayoutInt(0);
	}
	else if( (m_pcEditDoc->m_cLayoutMgr.GetLineCount() + 2 )- GetTextArea().m_nViewRowNum < nPos ){
		nPos = ( m_pcEditDoc->m_cLayoutMgr.GetLineCount() + CLayoutInt(2) ) - GetTextArea().m_nViewRowNum;
		if( nPos < 0 ){
			nPos = CLayoutInt(0);
		}
	}
	if( GetTextArea().GetViewTopLine() == nPos ){
		return CLayoutInt(0);	//	スクロール無し。
	}
	/* 垂直スクロール量（行数）の算出 */
	nScrollRowNum = GetTextArea().GetViewTopLine() - nPos;

	/* スクロール */
	if( t_abs( nScrollRowNum ) >= GetTextArea().m_nViewRowNum ){
		GetTextArea().SetViewTopLine( CLayoutInt(nPos) );
		CMyRect rect = GetTextArea().GetAreaRect();
		rect.left = 0;
		::InvalidateRect( GetHwnd(), &rect, TRUE );
	}else{
		rcScrol.left = 0;
		rcScrol.right = GetTextArea().GetAreaRight();
		rcScrol.top = GetTextArea().GetAreaTop();
		rcScrol.bottom = GetTextArea().GetAreaBottom();
		if( nScrollRowNum > 0 ){
			rcScrol.bottom =
				GetTextArea().GetAreaBottom() -
				(Int)nScrollRowNum * GetTextMetrics().GetHankakuDy();
			GetTextArea().SetViewTopLine( CLayoutInt(nPos) );
			rcClip.left = 0;
			rcClip.right = GetTextArea().GetAreaRight();
			rcClip.top = GetTextArea().GetAreaTop();
			rcClip.bottom =
				GetTextArea().GetAreaTop() + (Int)nScrollRowNum * GetTextMetrics().GetHankakuDy();
		}
		else if( nScrollRowNum < 0 ){
			rcScrol.top =
				GetTextArea().GetAreaTop() - (Int)nScrollRowNum * GetTextMetrics().GetHankakuDy();
			GetTextArea().SetViewTopLine( CLayoutInt(nPos) );
			rcClip.left = 0;
			rcClip.right = GetTextArea().GetAreaRight();
			rcClip.top =
				GetTextArea().GetAreaBottom() +
				(Int)nScrollRowNum * GetTextMetrics().GetHankakuDy();
			rcClip.bottom = GetTextArea().GetAreaBottom();
		}
		if( GetDrawSwitch() ){
			RECT rcClip2 = {0,0,0,0};
			ScrollDraw(nScrollRowNum, CLayoutInt(0), rcScrol, rcClip, rcClip2);
		}
	}

	/* スクロールバーの状態を更新する */
	AdjustScrollBars(bRedrawScrollBar);

	/* キャレットの表示・更新 */
	GetCaret().ShowEditCaret();

	MiniMapRedraw(false);

	return -nScrollRowNum;	//方向が逆なので符号反転が必要
}

/*! 指定左端桁位置へスクロール

	@param nPos [in] スクロール位置
	@param bRedrawScrollBar [in] スクロールバーを再描画するかどうか
	@retval 実際にスクロールした桁数 (正:右方向/負:左方向)

	@date 2004.09.11 genta 桁数を戻り値として返すように．(同期スクロール用)
	@date 2008.06.08 ryoji 水平スクロール範囲にぶら下げ余白を追加
	@date 2009.08.28 nasukoji	「折り返さない」選択時右に行き過ぎないようにする
*/
CLayoutInt CEditView::ScrollAtH( CLayoutInt nPos, BOOL bRedrawScrollBar )
{
	RECT		rcScrol;
	RECT		rcClip2;
	if( nPos < 0 ){
		nPos = CLayoutInt(0);
	}
	//	Aug. 18, 2003 ryoji 変数のミスを修正
	//	ウィンドウの幅をきわめて狭くしたときに編集領域が行番号から離れてしまうことがあった．
	//	Aug. 14, 2005 genta 折り返し幅をLayoutMgrから取得するように
	else if( GetRightEdgeForScrollBar() + GetWrapOverhang() - GetTextArea().m_nViewColNum  < nPos ){
		nPos = GetRightEdgeForScrollBar() + GetWrapOverhang() - GetTextArea().m_nViewColNum ;
		//	May 29, 2004 genta 折り返し幅よりウィンドウ幅が大きいときにWM_HSCROLLが来ると
		//	nPosが負の値になることがあり，その場合にスクロールバーから編集領域が
		//	離れてしまう．
		if( nPos < 0 )
			nPos = CLayoutInt(0);
	}
	if( GetTextArea().GetViewLeftCol() == nPos ){
		return CLayoutInt(0);
	}
	/* 水平スクロール量（文字数）の算出 */
	const CLayoutInt	nScrollColNum = GetTextArea().GetViewLeftCol() - nPos;

	/* スクロール */
	if( t_abs( nScrollColNum ) >= GetTextArea().m_nViewColNum /*|| abs( nScrollRowNum ) >= GetTextArea().m_nViewRowNum*/ ){
		GetTextArea().SetViewLeftCol( nPos );
		::InvalidateRect( GetHwnd(), nullptr, TRUE );
	}else{
		rcScrol.left = 0;
		rcScrol.right = GetTextArea().GetAreaRight();
		rcScrol.top = GetTextArea().GetAreaTop();
		rcScrol.bottom = GetTextArea().GetAreaBottom();
		int nScrollColPxWidth = GetTextMetrics().GetCharPxWidth(nScrollColNum);
		if( nScrollColNum > 0 ){
			rcScrol.left = GetTextArea().GetAreaLeft();
			rcScrol.right =
				GetTextArea().GetAreaRight() - nScrollColPxWidth;
			rcClip2.left = GetTextArea().GetAreaLeft();
			rcClip2.right = GetTextArea().GetAreaLeft() + nScrollColPxWidth;
			rcClip2.top = GetTextArea().GetAreaTop();
			rcClip2.bottom = GetTextArea().GetAreaBottom();
		}
		else if( nScrollColNum < 0 ){
			rcScrol.left = GetTextArea().GetAreaLeft() - nScrollColPxWidth;
			rcClip2.left =
				GetTextArea().GetAreaRight() + nScrollColPxWidth;
			rcClip2.right = GetTextArea().GetAreaRight();
			rcClip2.top = GetTextArea().GetAreaTop();
			rcClip2.bottom = GetTextArea().GetAreaBottom();
		}
		GetTextArea().SetViewLeftCol( nPos );
		if( GetDrawSwitch() ){
			RECT rcClip = {0,0,0,0};
			ScrollDraw(CLayoutInt(0), nScrollColNum, rcScrol, rcClip, rcClip2);
		}
	}
	//	2006.1.28 aroka 判定条件誤り修正 (バーが消えてもスクロールしない)
	// 先にAdjustScrollBarsを呼んでしまうと、二度目はここまでこないので、
	// GetRuler().DispRulerが呼ばれない。そのため、順序を入れ替えた。
	GetRuler().SetRedrawFlag(); // ルーラーを再描画する。
	HDC hdc = ::GetDC( GetHwnd() );
	GetRuler().DispRuler( hdc );
	::ReleaseDC( GetHwnd(), hdc );

	/* スクロールバーの状態を更新する */
	AdjustScrollBars(bRedrawScrollBar);

	/* キャレットの表示・更新 */
	GetCaret().ShowEditCaret();

	return -nScrollColNum;	//方向が逆なので符号反転が必要
}

void CEditView::ScrollDraw(CLayoutInt nScrollRowNum, CLayoutInt nScrollColNum, const RECT& rcScroll, const RECT& rcClip, const RECT& rcClip2)
{
	const CTextArea& area = GetTextArea();

	// 背景は画面に対して固定か
	bool bBackImgFixed = IsBkBitmap() &&
		((0 != nScrollRowNum && !m_pTypeData->m_backImgScrollY) ||
		 (0 != nScrollColNum && !m_pTypeData->m_backImgScrollX));
	if( bBackImgFixed ){
		CMyRect rcBody = area.GetAreaRect();
		rcBody.left = 0; // 行番号も移動
		rcBody.top = area.GetRulerHeight();
		InvalidateRect(&rcBody, FALSE);
	}else{
		int nScrollColPxWidth = GetTextMetrics().GetCharPxWidth(nScrollColNum);
		ScrollWindowEx(
			nScrollColPxWidth,	// 水平スクロール量
			(Int)nScrollRowNum * GetTextMetrics().GetHankakuDy(),	// 垂直スクロール量
			&rcScroll,	/* スクロール長方形の構造体のアドレス */
			nullptr, nullptr , nullptr, SW_ERASE | SW_INVALIDATE
		);
		// From Here 2007.09.09 Moca 互換BMPによる画面バッファ
		if( m_hbmpCompatBMP ){
			// 互換BMPもスクロール処理のためにBitBltで移動させる
			::BitBlt(
				m_hdcCompatDC,
				rcScroll.left + nScrollColPxWidth,
				rcScroll.top  + (Int)nScrollRowNum * GetTextMetrics().GetHankakuDy(),
				rcScroll.right - rcScroll.left, rcScroll.bottom - rcScroll.top,
				m_hdcCompatDC, rcScroll.left, rcScroll.top, SRCCOPY
			);
		}

		if( 0 < area.GetTopYohaku() &&
		  IsBkBitmap() &&
		  ((0 != nScrollRowNum && m_pTypeData->m_backImgScrollY) || (0 != nScrollColNum && m_pTypeData->m_backImgScrollX)) ){
			// Scrollのときにルーラー余白更新
			CMyRect rcTopYohaku;
			if( CTypeSupport(this, COLORIDX_TEXT).GetBackColor() == CTypeSupport(this, COLORIDX_GYOU).GetBackColor() ){
				rcTopYohaku.left = 0;
			}else{
				rcTopYohaku.left = area.GetLineNumberWidth();
			}
			rcTopYohaku.top  = area.GetRulerHeight();
			rcTopYohaku.right  = area.GetAreaRight();
			rcTopYohaku.bottom = area.GetAreaTop();
			HDC hdcSelf = GetDC();
			HDC hdcBgImg = m_hdcCompatDC ? m_hdcCompatDC : CreateCompatibleDC(hdcSelf);
			HBITMAP hOldBmp = (HBITMAP)::SelectObject(hdcBgImg, m_pcEditDoc->m_hBackImg);
			DrawBackImage(hdcSelf, rcTopYohaku, hdcBgImg);
			SelectObject(hdcBgImg, hOldBmp);
			ReleaseDC(hdcSelf);
			if( !m_hdcCompatDC ){
				DeleteObject(hdcBgImg);
			}
		}
		if( IsBkBitmap() && 0 != nScrollColNum && m_pTypeData->m_backImgScrollX ){
			// 行番号背景のために更新
			CMyRect rcLineNum;
			area.GenerateLineNumberRect(&rcLineNum);
			InvalidateRect( &rcLineNum, FALSE );
		}
	}
	// カーソルの縦線がテキストと行番号の隙間にあるとき、スクロール時に縦線領域を更新
	if( nScrollColNum != 0 && m_nOldCursorLineX == GetTextArea().GetAreaLeft() - 1 ){
		RECT rcClip3;
		rcClip3.left   = m_nOldCursorLineX - (m_nOldCursorVLineWidth - 1);
		rcClip3.right  = m_nOldCursorLineX + 1;
		rcClip3.top    = GetTextArea().GetAreaTop();
		rcClip3.bottom = GetTextArea().GetAreaBottom();
		InvalidateRect( &rcClip3, FALSE );
	}
	// To Here 2007.09.09 Moca

	if( nScrollRowNum != 0 ){
		InvalidateRect( &rcClip );
		if( nScrollColNum != 0 ){
			RECT lineNumClip;
			GetTextArea().GenerateLineNumberRect(&lineNumClip);
			InvalidateRect( &lineNumClip, FALSE );
		}
	}
	if( nScrollColNum != 0 ){
		InvalidateRect( &rcClip2, FALSE );
	}
	UpdateWindow();
}

void CEditView::MiniMapRedraw(bool bUpdateAll)
{
	if( this == &GetEditWnd().GetActiveView() && GetEditWnd().GetMiniMap().GetHwnd() ){
		CEditView& miniMap = GetEditWnd().GetMiniMap();
		CLayoutYInt nViewTop = miniMap.m_nPageViewTop;
		CLayoutYInt nViewBottom = miniMap.m_nPageViewBottom;
		CLayoutYInt nDiff = nViewTop - GetTextArea().GetViewTopLine();
		CLayoutYInt nDrawTopTop;
		CLayoutYInt nDrawTopBottom;
		bool bUpdate = (t_abs(nDiff) > nViewBottom - nViewTop) || bUpdateAll;
		bool bUpdateOne = false;
		if( bUpdate ){
			if( nViewTop == GetTextArea().GetViewTopLine() ){
				// OnSize:下だけ伸縮する
				bUpdateOne = true;
				nDrawTopTop = t_min(nViewBottom, GetTextArea().GetBottomLine());
				nDrawTopBottom = t_max(nViewBottom, GetTextArea().GetBottomLine());
			}else{
				nDrawTopTop = nViewTop;
				nDrawTopBottom = nViewBottom;
			}
		}else{
			if( nDiff < 0 ){
				// 上に移動
				nDrawTopTop = GetTextArea().GetViewTopLine();
				nDrawTopBottom = nViewTop;
			}else{
				// 下に移動
				nDrawTopTop = nViewTop;
				nDrawTopBottom = GetTextArea().GetViewTopLine();
			}
		}
		RECT rcMiniMap;
		rcMiniMap.left = 0;
		rcMiniMap.right = miniMap.GetTextArea().GetAreaRight();
		rcMiniMap.top = miniMap.GetTextArea().GenerateYPx(nDrawTopTop);
		rcMiniMap.bottom = miniMap.GetTextArea().GenerateYPx(nDrawTopBottom);
		::InvalidateRect( miniMap.GetHwnd(), &rcMiniMap, FALSE );
		::UpdateWindow( miniMap.GetHwnd() );

		if( bUpdateOne ){
			return;
		}
		CLayoutYInt nDrawBottomTop;
		CLayoutYInt nDrawBottomBottom;
		if( bUpdate ){
			nDrawBottomTop = GetTextArea().GetViewTopLine();
			nDrawBottomBottom = GetTextArea().GetBottomLine();
		}else{
			if( nDiff < 0 ){
				// 上に移動
				nDrawBottomTop = GetTextArea().GetBottomLine();
				nDrawBottomBottom = nViewBottom;
			}else{
				// 下に移動
				nDrawBottomTop = nViewBottom;
				nDrawBottomBottom = GetTextArea().GetBottomLine();
			}
		}
		rcMiniMap.left = 0;
		rcMiniMap.right = miniMap.GetTextArea().GetAreaRight();
		rcMiniMap.top = miniMap.GetTextArea().GenerateYPx(nDrawBottomTop);
		rcMiniMap.bottom = miniMap.GetTextArea().GenerateYPx(nDrawBottomBottom);
		::InvalidateRect( miniMap.GetHwnd(), &rcMiniMap, FALSE );
		::UpdateWindow( miniMap.GetHwnd() );
	}
}

/*!	垂直同期スクロール

	垂直同期スクロールがONならば，対応するウィンドウを指定行数同期スクロールする
	
	@param line [in] スクロール行数 (正:下方向/負:上方向/0:何もしない)
	
	@author asa-o
	@date 2001.06.20 asa-o 新規作成
	@date 2004.09.11 genta 関数化

	@note 動作の詳細は設定や機能拡張により変更になる可能性がある

*/
void CEditView::SyncScrollV( CLayoutInt line )
{
	if( GetDllShareData().m_Common.m_sWindow.m_bSplitterWndVScroll && line != 0 
		&& GetEditWnd().IsEnablePane(m_nMyIndex^0x01)
		&& 0 <= m_nMyIndex
	){
		CEditView&	editView = GetEditWnd().GetView(m_nMyIndex^0x01);
#if 0
		//	差分を保ったままスクロールする場合
		editView.ScrollByV( line );
#else
		editView.ScrollAtV( GetTextArea().GetViewTopLine() );
#endif
	}
}

/*!	水平同期スクロール

	水平同期スクロールがONならば，対応するウィンドウを指定行数同期スクロールする．
	
	@param col [in] スクロール桁数 (正:右方向/負:左方向/0:何もしない)
	
	@author asa-o
	@date 2001.06.20 asa-o 新規作成
	@date 2004.09.11 genta 関数化

	@note 動作の詳細は設定や機能拡張により変更になる可能性がある
*/
void CEditView::SyncScrollH( CLayoutInt col )
{
	if( GetDllShareData().m_Common.m_sWindow.m_bSplitterWndHScroll && col != 0
		&& GetEditWnd().IsEnablePane(m_nMyIndex^0x02)
		&& 0 <= m_nMyIndex
	){
		CEditView&	cEditView = GetEditWnd().GetView(m_nMyIndex^0x02);
		HDC			hdc = ::GetDC( cEditView.GetHwnd() );
		
#if 0
		//	差分を保ったままスクロールする場合
		cEditView.ScrollByH( col );
#else
		cEditView.ScrollAtH( GetTextArea().GetViewLeftCol() );
#endif
		GetRuler().SetRedrawFlag(); //2002.02.25 Add By KK スクロール時ルーラー全体を描きなおす。
		GetRuler().DispRuler( hdc );
		::ReleaseDC( GetHwnd(), hdc );
	}
}

/** 折り返し桁以後のぶら下げ余白計算
	@date 2008.06.08 ryoji 新規作成
*/
CLayoutInt CEditView::GetWrapOverhang( void ) const
{
	CLayoutInt nMargin = GetTextMetrics().GetLayoutXDefault(CKetaXInt(1));	// 折り返し記号
	if (!m_pTypeData->m_bKinsokuHide) {	// ぶら下げを隠す時はスキップ	2012/11/30 Uchi
		if( m_pTypeData->m_bKinsokuRet )
			nMargin += GetTextMetrics().GetLayoutXDefault(CKetaXInt(1));	// 改行ぶら下げ
		if( m_pTypeData->m_bKinsokuKuto )
			nMargin += GetTextMetrics().GetLayoutXDefault(CKetaXInt(2));	// 句読点ぶら下げ
	}
	return CLayoutInt( nMargin );
}

/** 「右端で折り返す」用にビューの桁数から折り返し桁数を計算する
	@param nViewColNum	[in] ビューの桁数
	@retval 折り返し桁数
	@date 2008.06.08 ryoji 新規作成
*/
CKetaXInt CEditView::ViewColNumToWrapColNum( CLayoutXInt nViewColNum ) const
{
	// ぶら下げ余白を差し引く
	CKetaXInt nKeta = CKetaXInt((Int)(nViewColNum - GetWrapOverhang())) / (Int)GetTextMetrics().GetLayoutXDefault();

	// MINLINEKETAS未満の時はMINLINEKETASで折り返しとする
	if( nKeta < CKetaXInt(MINLINEKETAS) ){
		nKeta = CKetaXInt(MINLINEKETAS);		// 折り返し幅の最小桁数に設定
	}

	return nKeta;
}

/*!
	@brief  スクロールバー制御用に右端座標を取得する

	「折り返さない」
		フリーカーソル状態の時はテキストの幅よりも右側へカーソルが移動できる
		ので、それを考慮したスクロールバーの制御が必要。
		本関数は、下記の内で最も大きな値（右端の座標）を返す。
		　・テキストの右端
		　・キャレット位置
		　・選択範囲の右端
	
	「指定桁で折り返す」
	「右端で折り返す」
		上記の場合折り返し桁以後のぶら下げ余白計算

	@return     右端のレイアウト座標を返す

	@note   「折り返さない」選択時は、スクロール後にキャレットが見えなく
	        ならない様にするために右マージンとして半角3個分固定で加算する。

	@date 2009.08.28 nasukoji	新規作成
*/
CLayoutInt CEditView::GetRightEdgeForScrollBar( void )
{
	// 折り返し桁以後のぶら下げ余白計算
	CLayoutXInt nWidth = m_pcEditDoc->m_cLayoutMgr.GetMaxLineLayout() + GetWrapOverhang();
	
	if( m_pcEditDoc->m_nTextWrapMethodCur == WRAP_NO_TEXT_WRAP ){
		CLayoutInt nRightEdge = m_pcEditDoc->m_cLayoutMgr.GetMaxTextWidth();	// テキストの最大幅

		// 選択範囲あり かつ 範囲の右端がテキストの幅より右側
		if( GetSelectionInfo().IsTextSelected() ){
			// 開始位置・終了位置のより右側にある方で比較
			if( GetSelectionInfo().m_sSelect.GetFrom().GetX2() < GetSelectionInfo().m_sSelect.GetTo().GetX2() ){
				if( nRightEdge < GetSelectionInfo().m_sSelect.GetTo().GetX2() )
					nRightEdge = GetSelectionInfo().m_sSelect.GetTo().GetX2();
			}else{
				if( nRightEdge < GetSelectionInfo().m_sSelect.GetFrom().GetX2() )
					nRightEdge = GetSelectionInfo().m_sSelect.GetFrom().GetX2();
			}
		}

		// フリーカーソルモード かつ キャレット位置がテキストの幅より右側
		if( GetDllShareData().m_Common.m_sGeneral.m_bIsFreeCursorMode && nRightEdge < GetCaret().GetCaretLayoutPos().GetX2() )
			nRightEdge = GetCaret().GetCaretLayoutPos().GetX2();

		// 右マージン分（3桁）を考慮しつつnWidthを超えないようにする
		CLayoutXInt layout3Keta = GetTextMetrics().GetLayoutXDefault(CKetaXInt(3));
		nWidth = t_min( nRightEdge + layout3Keta, nWidth );
	}

	return nWidth;
}
