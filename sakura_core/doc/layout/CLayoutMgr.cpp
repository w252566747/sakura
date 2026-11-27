/*!	@file
	@brief テキストのレイアウト情報管理

	@author Norio Nakatani
*/
/*
	Copyright (C) 1998-2001, Norio Nakatani
	Copyright (C) 2002, MIK, YAZAKI, genta, aroka
	Copyright (C) 2003, genta, Moca
	Copyright (C) 2004, genta, Moca
	Copyright (C) 2005, D.S.Koba, Moca
	Copyright (C) 2009, ryoji, nasukoji
	Copyright (C) 2018-2022, Sakura Editor Organization

	This source code is designed for sakura editor.
	Please contact the copyright holder to use this code for other purpose.
*/
#include "StdAfx.h"
#include "doc/layout/CLayoutMgr.h"
#include "doc/layout/CLayout.h"/// 2002/2/10 aroka
#include "doc/CEditDoc.h"
#include "doc/CDocReader.h" // for _DEBUG
#include "doc/CDocEditor.h"
#include "doc/logic/CDocLine.h"/// 2002/2/10 aroka
#include "doc/logic/CDocLineMgr.h"/// 2002/2/10 aroka
#include "charset/charcode.h"
#include "mem/CMemory.h"/// 2002/2/10 aroka
#include "mem/CMemoryIterator.h" // 2006.07.29 genta
#include "mem/CPoolResource.h"
#include "view/CViewFont.h"
#include "view/CTextMetrics.h"
#include "basis/SakuraBasis.h"
#include "agent/CSearchAgent.h"
#include "view/colors/CWordMarker.h"
#include "debug/CRunningTimer.h"
#include "charset/charcode.h"
#include "config/app_constants.h"
#include "util/window.h"
#include "parse/CodeFoldParser.h"
#include "window/EnhancementToolbarSettings.h"
#include <algorithm>
#include <cwctype>
#include <limits>
#include <string_view>

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                        生成と破棄                           //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

CLayoutMgr::CLayoutMgr()
: m_getIndentOffset( &CLayoutMgr::getIndentOffset_Normal )	//	Oct. 1, 2002 genta	//	Nov. 16, 2002 メンバー関数ポインタにはクラス名が必要
  , m_layoutMemRes(new CPoolResource<CLayout>())
  //, m_layoutMemRes(new std::pmr::unsynchronized_pool_resource()) // メモリ使用量が大きい為に使用しない
{
	m_pcDocLineMgr = nullptr;
	m_pTypeConfig = nullptr;
	m_nMaxLineKetas = CKetaXInt(MAXLINEKETAS);
	m_nTabSpace = CKetaXInt(4);
	m_tsvInfo.m_nTsvMode = TSV_MODE_NONE;
	m_pszKinsokuHead_1.clear();						/* 行頭禁則 */	//@@@ 2002.04.08 MIK
	m_pszKinsokuTail_1.clear();						/* 行末禁則 */	//@@@ 2002.04.08 MIK
	m_pszKinsokuKuto_1.clear();						/* 句読点ぶらさげ */	//@@@ 2002.04.17 MIK

	m_nTextWidth = CLayoutInt(0);			// テキスト最大幅の記憶		// 2009.08.28 nasukoji
	m_nTextWidthMaxLine = CLayoutInt(0);	// 最大幅のレイアウト行		// 2009.08.28 nasukoji

	Init();
}

CLayoutMgr::~CLayoutMgr()
{
	_Empty();

	m_pszKinsokuHead_1.clear();	/* 行頭禁則 */
	m_pszKinsokuTail_1.clear();	/* 行末禁則 */	//@@@ 2002.04.08 MIK
	m_pszKinsokuKuto_1.clear();	/* 句読点ぶらさげ */	//@@@ 2002.04.17 MIK
}

/*
||
|| 行データ管理クラスのポインタを初期化します
||
*/
void CLayoutMgr::Create( CEditDoc* pcEditDoc, CDocLineMgr* pcDocLineMgr )
{
	m_bRealtimeFilterActive = false;
	m_vRealtimeFilterVisibleLines.clear();
	_Empty();
	ResetCodeFolds();
	Init();
	//	Jun. 20, 2003 genta EditDocへのポインタ追加
	m_pcEditDoc = pcEditDoc;
	m_pcDocLineMgr = pcDocLineMgr;
}

void CLayoutMgr::Init()
{
	m_pLayoutTop = nullptr;
	m_pLayoutBot = nullptr;
	m_nPrevReferLine = CLayoutInt(0);
	m_pLayoutPrevRefer = nullptr;
	m_nLines = CLayoutInt(0);			/* 全物理行数 */
	m_nVisibleLines = CLayoutInt(0);
	m_nLineTypeBot = COLORIDX_DEFAULT;

	// EOFレイアウト位置記憶	//2006.10.07 Moca
	m_nEOFLine = CLayoutInt(-1);
	m_nEOFColumn = CLayoutInt(-1);
}

void CLayoutMgr::_Empty()
{
	CLayout* pLayout = m_pLayoutTop;
	while( pLayout ){
		CLayout* pLayoutNext = pLayout->GetNextLayout();
		pLayout->~CLayout();
		m_layoutMemRes->deallocate(pLayout, sizeof(CLayout), alignof(CLayout));
		pLayout = pLayoutNext;
	}
}

/*! レイアウト情報の変更
	@param bDoLayout [in] レイアウト情報の再作成
	@param refType [in] タイプ別設定
*/
void CLayoutMgr::SetLayoutInfo(
	bool				bDoLayout,
	bool				bBlockingHook,
	const STypeConfig&	refType,
	CKetaXInt			nTabSpace,
	int					nTsvMode,
	CKetaXInt			nMaxLineKetas,
	CLayoutXInt			nCharLayoutXPerKeta,
	const LOGFONT*		pLogfont,
	CCharWidthCache&	cache
)
{
	MY_RUNNINGTIMER( cRunningTimer, L"CLayoutMgr::SetLayoutInfo" );

	assert_warning( (!bDoLayout && m_nMaxLineKetas == nMaxLineKetas) || bDoLayout );
	assert_warning( (!bDoLayout && m_nTabSpace == refType.m_nTabSpace) || bDoLayout );

	//タイプ別設定
	// 言語タイプが変わると解析規則も変わるため、以前の折りたたみ位置を持ち越さない。
	if (m_pTypeConfig && m_pTypeConfig != &refType) ResetCodeFolds();
	m_pTypeConfig = &refType;
	m_nMaxLineKetas = nMaxLineKetas;
	m_nTabSpace = nTabSpace;
	int nTsvModeOld = m_tsvInfo.m_nTsvMode;
	m_tsvInfo.m_nTsvMode = nTsvMode;
	if (nTsvModeOld != nTsvMode && nTsvMode != TSV_MODE_NONE) {
		m_tsvInfo.CalcTabLength(this->m_pcDocLineMgr);
	}
	m_nSpacing = DpiScaleX(refType.m_nColumnSpace);
	if( nCharLayoutXPerKeta == -1 )
	{
		// Viewが持ってるフォント情報は古い、しょうがないので自分で作る
		HWND hwnd = nullptr;
		HDC hdc = ::GetDC(hwnd);
		CViewFont viewFont(pLogfont);
		CTextMetrics temp;
		temp.Update(hdc, viewFont.GetFontHan(), DpiScaleY(refType.m_nLineSpace), DpiScaleX(refType.m_nColumnSpace));
		m_nCharLayoutXPerKeta = temp.GetHankakuWidth() + DpiScaleX(m_pTypeConfig->m_nColumnSpace);
		::ReleaseDC(hwnd, hdc);
	}else{
		m_nCharLayoutXPerKeta = nCharLayoutXPerKeta;
	}
	// 最大文字幅の計算
	m_tsvInfo.m_nMaxCharLayoutX = cache.CalcPxWidthByFont(L'W');
	if (m_tsvInfo.m_nMaxCharLayoutX < m_nCharLayoutXPerKeta) {
		m_tsvInfo.m_nMaxCharLayoutX = m_nCharLayoutXPerKeta;
	}

	//	Oct. 1, 2002 genta タイプによって処理関数を変更する
	//	数が増えてきたらテーブルにすべき
	switch ( refType.m_nIndentLayout ){	/* 折り返しは2行目以降を字下げ表示 */	//@@@ 2002.09.29 YAZAKI
	case 1:
		//	Nov. 16, 2002 メンバー関数ポインタにはクラス名が必要
		m_getIndentOffset = &CLayoutMgr::getIndentOffset_Tx2x;
		break;
	case 2:
		m_getIndentOffset = &CLayoutMgr::getIndentOffset_LeftSpace;
		break;
	default:
		m_getIndentOffset = &CLayoutMgr::getIndentOffset_Normal;
		break;
	}

	//句読点ぶら下げ文字	// 2009.08.07 ryoji
	//refType.m_szKinsokuKuto → m_pszKinsokuKuto_1
	m_pszKinsokuKuto_1.clear();
	if(refType.m_bKinsokuKuto){	// 2009.08.06 ryoji m_bKinsokuKutoで振り分ける(Fix)
		for( const wchar_t* p = refType.m_szKinsokuKuto; *p; p++ ){
			m_pszKinsokuKuto_1.push_back_unique(*p);
		}
	}

	//行頭禁則文字
	//refType.m_szKinsokuHead → (句読点以外) m_pszKinsokuHead_1
	m_pszKinsokuHead_1.clear();
	for( const wchar_t* p = refType.m_szKinsokuHead; *p; p++ ){
		if(m_pszKinsokuKuto_1.exist(*p)){
			continue;
		}
		else{
			m_pszKinsokuHead_1.push_back_unique(*p);
		}
	}

	//行末禁則文字
	//refType.m_szKinsokuTail → m_pszKinsokuTail_1
	m_pszKinsokuTail_1.clear();
	for( const wchar_t* p = refType.m_szKinsokuTail; *p; p++ ){
		m_pszKinsokuTail_1.push_back_unique(*p);
	}

	//レイアウト
	if( bDoLayout ){
		_DoLayout(bBlockingHook);
	}
}

/*!
	@brief 指定された物理行のレイアウト情報を取得

	@param nLineNum [in] 物理行番号 (0～)
*/
const CLayout* CLayoutMgr::SearchLineByLayoutY(CLayoutInt nLineLayout) const
{
	if (CLayoutInt(0) > nLineLayout || nLineLayout >= m_nVisibleLines) {
		if (CLayoutInt(0) > nLineLayout) DEBUG_TRACE(L"CLayoutMgr::SearchLineByLayoutY() nLineNum = %d\n", nLineLayout);
		return nullptr;
	}

	auto isVisible = [this](const CLayout* layout) {
		return layout && !IsDisplayHiddenLine(layout->GetLogicLineNo());
	};
	auto remember = [this, nLineLayout](CLayout* layout) -> const CLayout* {
		m_pLayoutPrevRefer = layout;
		m_nPrevReferLine = nLineLayout;
		return layout;
	};

	if (m_pLayoutPrevRefer && m_nPrevReferLine >= CLayoutInt(0) && m_nPrevReferLine < m_nVisibleLines) {
		if (nLineLayout == m_nPrevReferLine && isVisible(m_pLayoutPrevRefer)) return m_pLayoutPrevRefer;
		if (nLineLayout > m_nPrevReferLine) {
			CLayoutInt visibleIndex = m_nPrevReferLine;
			for (CLayout* layout = m_pLayoutPrevRefer->GetNextLayout(); layout; layout = layout->GetNextLayout()) {
				if (!isVisible(layout)) continue;
				++visibleIndex;
				if (visibleIndex == nLineLayout) return remember(layout);
			}
		} else {
			CLayoutInt visibleIndex = m_nPrevReferLine;
			for (CLayout* layout = m_pLayoutPrevRefer->GetPrevLayout(); layout; layout = layout->GetPrevLayout()) {
				if (!isVisible(layout)) continue;
				--visibleIndex;
				if (visibleIndex == nLineLayout) return remember(layout);
			}
		}
	}

	if (nLineLayout < m_nVisibleLines / 2) {
		CLayoutInt visibleIndex(0);
		for (CLayout* layout = m_pLayoutTop; layout; layout = layout->GetNextLayout()) {
			if (!isVisible(layout)) continue;
			if (visibleIndex == nLineLayout) return remember(layout);
			++visibleIndex;
		}
	} else {
		CLayoutInt visibleIndex = m_nVisibleLines - CLayoutInt(1);
		for (CLayout* layout = m_pLayoutBot; layout; layout = layout->GetPrevLayout()) {
			if (!isVisible(layout)) continue;
			if (visibleIndex == nLineLayout) return remember(layout);
			--visibleIndex;
		}
	}
	return nullptr;
}

bool CLayoutMgr::IsCodeFoldSupported() const
{
	if (!enhancementtoolbar::IsEnabled(enhancementtoolbar::Group::CodeFolding) || !m_pTypeConfig) return false;
	auto lower = [](std::wstring value) {
		std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
		return value;
	};
	const std::wstring name = lower(m_pTypeConfig->m_szTypeName);
	const std::wstring exts = lower(m_pTypeConfig->m_szTypeExts);
	return name.find(L"curl") != std::wstring::npos || exts.find(L"scurl") != std::wstring::npos
		|| name.find(L"c/c++") != std::wstring::npos || name.find(L"c++") != std::wstring::npos
		|| name.find(L"java") != std::wstring::npos || name.find(L"javascript") != std::wstring::npos
		|| name.find(L"json") != std::wstring::npos || name.find(L"xml") != std::wstring::npos
		|| name.find(L"html") != std::wstring::npos || name.find(L"sql") != std::wstring::npos
		|| exts.find(L".cpp") != std::wstring::npos || exts.find(L"cpp") != std::wstring::npos
		|| exts.find(L".java") != std::wstring::npos || exts.find(L"java") != std::wstring::npos
		|| exts.find(L".js") != std::wstring::npos || exts.find(L"js") != std::wstring::npos
		|| exts.find(L"json") != std::wstring::npos || exts.find(L"xml") != std::wstring::npos
		|| exts.find(L"html") != std::wstring::npos || exts.find(L"sql") != std::wstring::npos
		|| exts.find(L"plsql") != std::wstring::npos;
}

bool CLayoutMgr::EnsureCodeFoldRegions() const
{
	if (m_bCodeFoldRegionsValid) return true;
	CLayoutMgr* self = const_cast<CLayoutMgr*>(this);
	self->m_vCodeFoldRegions.clear();
	if (!IsCodeFoldSupported() || !m_pcDocLineMgr) {
		self->m_bCodeFoldRegionsValid = true;
		return false;
	}

	codefold::Language language = codefold::Language::CLike;
	std::wstring typeName = m_pTypeConfig->m_szTypeName;
	std::transform(typeName.begin(), typeName.end(), typeName.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
	std::wstring extensions = m_pTypeConfig->m_szTypeExts;
	std::transform(extensions.begin(), extensions.end(), extensions.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
	if (typeName.find(L"curl") != std::wstring::npos || extensions.find(L"scurl") != std::wstring::npos) language = codefold::Language::Curl;
	else if (typeName.find(L"xml") != std::wstring::npos || typeName.find(L"html") != std::wstring::npos || extensions.find(L"xml") != std::wstring::npos || extensions.find(L"html") != std::wstring::npos) language = codefold::Language::Xml;
	else if (typeName.find(L"sql") != std::wstring::npos || extensions.find(L"sql") != std::wstring::npos || extensions.find(L"plsql") != std::wstring::npos) language = codefold::Language::Sql;
	else if (typeName.find(L"javascript") != std::wstring::npos || extensions.find(L".js") != std::wstring::npos || extensions.find(L"js") == 0) language = codefold::Language::JavaScript;
	else if (typeName.find(L"json") != std::wstring::npos || extensions.find(L"json") != std::wstring::npos) language = codefold::Language::Json;
	else if (typeName.find(L"java") != std::wstring::npos || extensions.find(L"java") != std::wstring::npos) language = codefold::Language::Java;

	try {
		std::vector<std::wstring_view> lines;
		lines.reserve(static_cast<size_t>(m_pcDocLineMgr->GetLineCount()));
		for (const CDocLine* line = m_pcDocLineMgr->GetDocLineTop(); line; line = line->GetNextLine()) {
			const CLogicInt length = line->GetLengthWithoutEOL();
			lines.emplace_back(line->GetPtr(), length > CLogicInt(0) ? static_cast<size_t>(length) : 0u);
		}
		self->m_vCodeFoldRegions = codefold::Parse(lines, language);
		self->m_bCodeFoldRegionsValid = true;
		return true;
	} catch (...) {
		self->m_vCodeFoldRegions.clear();
		self->m_bCodeFoldRegionsValid = true;
		return false;
	}
}

const codefold::Region* CLayoutMgr::FindCodeFoldAtLine(CLogicInt logicLine) const
{
	if (logicLine < CLogicInt(0)) return nullptr;
	if (!EnsureCodeFoldRegions()) return nullptr;
	auto it = std::lower_bound(m_vCodeFoldRegions.begin(), m_vCodeFoldRegions.end(), static_cast<size_t>(logicLine),
		[](const codefold::Region& region, size_t line) { return region.firstLine < line; });
	return it != m_vCodeFoldRegions.end() && it->firstLine == static_cast<size_t>(logicLine) ? &*it : nullptr;
}

const codefold::Region* CLayoutMgr::FindCodeFoldContainingLine(CLogicInt logicLine, bool outermost) const
{
	if (logicLine < CLogicInt(0) || !EnsureCodeFoldRegions()) return nullptr;
	const size_t line = static_cast<size_t>(logicLine);
	const codefold::Region* best = nullptr;
	for (const auto& region : m_vCodeFoldRegions) {
		if (region.firstLine > line || region.lastLine < line) continue;
		if (!best || (outermost ? region.depth < best->depth : region.depth > best->depth)) best = &region;
	}
	return best;
}

const codefold::Region* CLayoutMgr::FindCollapsedCodeFoldContainingLine(CLogicInt logicLine) const
{
	if (logicLine < CLogicInt(0)) return nullptr;
	const size_t line = static_cast<size_t>(logicLine);
	const codefold::Region* best = nullptr;
	for (const auto& region : m_vCollapsedCodeFolds) {
		if (region.firstLine >= line || region.lastLine < line) continue;
		if (!best || region.depth < best->depth) best = &region;
	}
	return best;
}

bool CLayoutMgr::IsCodeFoldCollapsedAtLine(CLogicInt logicLine) const
{
	return std::any_of(m_vCollapsedCodeFolds.begin(), m_vCollapsedCodeFolds.end(), [logicLine](const codefold::Region& region) {
		return region.firstLine == static_cast<size_t>(logicLine);
	});
}

bool CLayoutMgr::IsCodeFoldHiddenLine(CLogicInt logicLine) const
{
	if (!enhancementtoolbar::IsEnabled(enhancementtoolbar::Group::CodeFolding)) return false;
	auto it = std::upper_bound(m_vFoldHiddenLogicalLines.begin(), m_vFoldHiddenLogicalLines.end(), logicLine,
		[](CLogicInt line, const FoldLineRange& range) { return line < range.first; });
	if (it == m_vFoldHiddenLogicalLines.begin()) return false;
	--it;
	return it->first <= logicLine && logicLine <= it->last;
}

bool CLayoutMgr::IsDisplayHiddenLine(CLogicInt logicLine) const
{
	if (logicLine < CLogicInt(0)) return false;
	if (IsCodeFoldHiddenLine(logicLine)) return true;
	if (!m_bRealtimeFilterActive) return false;
	const size_t line = static_cast<size_t>(logicLine);
	auto it = std::upper_bound(m_vRealtimeFilterVisibleLines.begin(), m_vRealtimeFilterVisibleLines.end(), line,
		[](size_t value, const FoldLineRange& range) { return value < static_cast<size_t>(range.first); });
	if (it != m_vRealtimeFilterVisibleLines.begin()) {
		--it;
		if (line <= static_cast<size_t>(it->last)) return false;
	}
	return true;
}

void CLayoutMgr::SetRealtimeFilterVisibleRanges(const std::vector<std::pair<size_t, size_t>>& ranges, bool active)
{
	if (!active && !m_bRealtimeFilterActive && m_vRealtimeFilterVisibleLines.empty()) return;
	m_vRealtimeFilterVisibleLines.clear();
	m_bRealtimeFilterActive = active;
	if (active) {
		m_vRealtimeFilterVisibleLines.reserve(ranges.size());
		for (const auto& range : ranges) {
			if (range.second < range.first || range.second > static_cast<size_t>(std::numeric_limits<Int>::max())) continue;
			m_vRealtimeFilterVisibleLines.push_back({ CLogicInt(range.first), CLogicInt(range.second) });
		}
	}
	RebuildVisibleLineCount();
}

bool CLayoutMgr::IsCodeFoldActive() const
{
	return enhancementtoolbar::IsEnabled(enhancementtoolbar::Group::CodeFolding)
		&& !m_vCollapsedCodeFolds.empty();
}

size_t CLayoutMgr::GetCodeFoldHiddenLineCount(CLogicInt logicLine) const
{
	const auto* region = FindCodeFoldAtLine(logicLine);
	return region && region->lastLine > region->firstLine ? region->lastLine - region->firstLine : 0;
}

void CLayoutMgr::RebuildFoldHiddenRanges()
{
	m_vFoldHiddenLogicalLines.clear();
	for (const auto& region : m_vCollapsedCodeFolds) {
		if (region.lastLine <= region.firstLine || region.lastLine > static_cast<size_t>(std::numeric_limits<Int>::max())) continue;
		m_vFoldHiddenLogicalLines.push_back({ CLogicInt(region.firstLine + 1), CLogicInt(region.lastLine) });
	}
	std::sort(m_vFoldHiddenLogicalLines.begin(), m_vFoldHiddenLogicalLines.end(), [](const FoldLineRange& a, const FoldLineRange& b) {
		return a.first < b.first || (a.first == b.first && a.last < b.last);
	});
	std::vector<FoldLineRange> merged;
	for (const auto& range : m_vFoldHiddenLogicalLines) {
		if (merged.empty() || range.first > merged.back().last + CLogicInt(1)) merged.push_back(range);
		else merged.back().last = t_max(merged.back().last, range.last);
	}
	m_vFoldHiddenLogicalLines.swap(merged);
	RebuildVisibleLineCount();
}

void CLayoutMgr::RebuildVisibleLineCount()
{
	m_nVisibleLines = CLayoutInt(0);
	for (const CLayout* layout = m_pLayoutTop; layout; layout = layout->GetNextLayout()) {
		if (!IsDisplayHiddenLine(layout->GetLogicLineNo())) ++m_nVisibleLines;
	}
	m_pLayoutPrevRefer = nullptr;
	m_nPrevReferLine = CLayoutInt(0);
	m_nEOFLine = CLayoutInt(-1);
	m_nEOFColumn = CLayoutInt(-1);
}

void CLayoutMgr::ExpandCodeFoldsContaining(CLogicInt logicLine)
{
	const size_t line = static_cast<size_t>(logicLine);
	const auto oldSize = m_vCollapsedCodeFolds.size();
	m_vCollapsedCodeFolds.erase(std::remove_if(m_vCollapsedCodeFolds.begin(), m_vCollapsedCodeFolds.end(), [line](const codefold::Region& region) {
		return region.firstLine < line && line <= region.lastLine;
	}), m_vCollapsedCodeFolds.end());
	if (m_vCollapsedCodeFolds.size() != oldSize) RebuildFoldHiddenRanges();
}

bool CLayoutMgr::ToggleCodeFoldAtLine(CLogicInt logicLine)
{
	const auto* found = FindCodeFoldAtLine(logicLine);
	if (!found) return false;
	const codefold::Region region = *found; // parser vector remains stable only until the next parse
	auto collapsed = std::find_if(m_vCollapsedCodeFolds.begin(), m_vCollapsedCodeFolds.end(), [&region](const codefold::Region& item) {
		return item.firstLine == region.firstLine && item.lastLine == region.lastLine;
	});
	if (collapsed != m_vCollapsedCodeFolds.end()) m_vCollapsedCodeFolds.erase(collapsed);
	else m_vCollapsedCodeFolds.push_back(region);
	RebuildFoldHiddenRanges();
	return true;
}

bool CLayoutMgr::ToggleCodeFoldRecursivelyAtLine(CLogicInt logicLine)
{
	const auto* found = FindCodeFoldAtLine(logicLine);
	if (!found) return false;
	const codefold::Region target = *found;
	const bool wasCollapsed = IsCodeFoldCollapsedAtLine(logicLine);
	if (wasCollapsed) {
		const size_t oldSize = m_vCollapsedCodeFolds.size();
		m_vCollapsedCodeFolds.erase(std::remove_if(m_vCollapsedCodeFolds.begin(), m_vCollapsedCodeFolds.end(),
			[&target](const codefold::Region& region) {
				return region.firstLine >= target.firstLine && region.lastLine <= target.lastLine;
			}), m_vCollapsedCodeFolds.end());
		if (m_vCollapsedCodeFolds.size() == oldSize) return false;
	} else {
		for (const auto& region : m_vCodeFoldRegions) {
			if (region.firstLine < target.firstLine || region.lastLine > target.lastLine) continue;
			if (std::none_of(m_vCollapsedCodeFolds.begin(), m_vCollapsedCodeFolds.end(), [&region](const codefold::Region& item) {
				return item.firstLine == region.firstLine;
			})) m_vCollapsedCodeFolds.push_back(region);
		}
	}
	RebuildFoldHiddenRanges();
	return true;
}

bool CLayoutMgr::FoldAllCodeBlocks()
{
	if (!EnsureCodeFoldRegions() || m_vCodeFoldRegions.empty()) return false;
	m_vCollapsedCodeFolds = m_vCodeFoldRegions;
	RebuildFoldHiddenRanges();
	return true;
}

bool CLayoutMgr::UnfoldAllCodeBlocks()
{
	if (m_vCollapsedCodeFolds.empty()) return false;
	m_vCollapsedCodeFolds.clear();
	RebuildFoldHiddenRanges();
	return true;
}

bool CLayoutMgr::FoldCodeBlocksAtDepth(size_t depth)
{
	if (!EnsureCodeFoldRegions()) return false;
	const size_t oldSize = m_vCollapsedCodeFolds.size();
	for (const auto& region : m_vCodeFoldRegions) {
		if (region.depth == depth && std::none_of(m_vCollapsedCodeFolds.begin(), m_vCollapsedCodeFolds.end(), [&region](const codefold::Region& item) {
			return item.firstLine == region.firstLine;
		})) m_vCollapsedCodeFolds.push_back(region);
	}
	if (m_vCollapsedCodeFolds.size() == oldSize) return false;
	RebuildFoldHiddenRanges();
	return true;
}

bool CLayoutMgr::FoldCodeBlocksToDepth(size_t oneBasedDepth)
{
	if (!EnsureCodeFoldRegions()) return false;
	const size_t minimumDepth = oneBasedDepth > 0 ? oneBasedDepth - 1 : 0;
	const size_t oldSize = m_vCollapsedCodeFolds.size();
	for (const auto& region : m_vCodeFoldRegions) {
		if (region.depth < minimumDepth) continue;
		if (std::none_of(m_vCollapsedCodeFolds.begin(), m_vCollapsedCodeFolds.end(), [&region](const codefold::Region& item) {
			return item.firstLine == region.firstLine;
		})) m_vCollapsedCodeFolds.push_back(region);
	}
	if (m_vCollapsedCodeFolds.size() == oldSize) return false;
	RebuildFoldHiddenRanges();
	return true;
}

CLayoutInt CLayoutMgr::GetFirstCollapsedFoldDisplayLine() const
{
	if (m_vCollapsedCodeFolds.empty()) return m_nVisibleLines;
	size_t firstFold = m_vCollapsedCodeFolds.front().firstLine;
	for (const auto& fold : m_vCollapsedCodeFolds) firstFold = std::min(firstFold, fold.firstLine);
	CLayoutInt visibleLine(0);
	for (const CLayout* layout = m_pLayoutTop; layout; layout = layout->GetNextLayout()) {
		if (layout->GetLogicLineNo() >= CLogicInt(firstFold) && layout->GetLogicOffset() == CLogicInt(0)) return visibleLine;
		if (!IsDisplayHiddenLine(layout->GetLogicLineNo())) ++visibleLine;
	}
	return CLayoutInt(0);
}

void CLayoutMgr::ResetCodeFolds()
{
	m_vCollapsedCodeFolds.clear();
	m_vFoldHiddenLogicalLines.clear();
	m_vCodeFoldRegions.clear();
	m_bCodeFoldRegionsValid = false;
	if (m_bRealtimeFilterActive) RebuildVisibleLineCount();
	else m_nVisibleLines = m_nLines;
	m_pLayoutPrevRefer = nullptr;
	m_nPrevReferLine = CLayoutInt(0);
	m_nEOFLine = CLayoutInt(-1);
	m_nEOFColumn = CLayoutInt(-1);
}

//@@@ 2002.09.23 YAZAKI CLayout*を作成するところは分離して、InsertLineNext()と共通化
void CLayoutMgr::AddLineBottom( CLayout* pLayout )
{
	if(	CLayoutInt(0) == m_nLines ){
		m_pLayoutBot = m_pLayoutTop = pLayout;
		m_pLayoutTop->m_pPrev = nullptr;
	}else{
		m_pLayoutBot->m_pNext = pLayout;
		pLayout->m_pPrev = m_pLayoutBot;
		m_pLayoutBot = pLayout;
	}
	pLayout->m_pNext = nullptr;
	m_nLines++;
	if (!IsDisplayHiddenLine(pLayout->GetLogicLineNo())) ++m_nVisibleLines;
	return;
}

//@@@ 2002.09.23 YAZAKI CLayout*を作成するところは分離して、AddLineBottom()と共通化
CLayout* CLayoutMgr::InsertLineNext( CLayout* pLayoutPrev, CLayout* pLayout )
{
	CLayout* pLayoutNext;

	if(	CLayoutInt(0) == m_nLines ){
		/* 初 */
		m_pLayoutBot = m_pLayoutTop = pLayout;
		m_pLayoutTop->m_pPrev = nullptr;
		m_pLayoutTop->m_pNext = nullptr;
	}
	else if( nullptr == pLayoutPrev ){
		/* 先頭に挿入 */
		m_pLayoutTop->m_pPrev = pLayout;
		pLayout->m_pPrev = nullptr;
		pLayout->m_pNext = m_pLayoutTop;
		m_pLayoutTop = pLayout;
	}else
	if( nullptr == pLayoutPrev->GetNextLayout() ){
		/* 最後に挿入 */
		m_pLayoutBot->m_pNext = pLayout;
		pLayout->m_pPrev = m_pLayoutBot;
		pLayout->m_pNext = nullptr;
		m_pLayoutBot = pLayout;
	}else{
		/* 途中に挿入 */
		pLayoutNext = pLayoutPrev->GetNextLayout();
		pLayoutPrev->m_pNext = pLayout;
		pLayoutNext->m_pPrev = pLayout;
		pLayout->m_pPrev = pLayoutPrev;
		pLayout->m_pNext = pLayoutNext;
	}
	m_nLines++;
	if (!IsDisplayHiddenLine(pLayout->GetLogicLineNo())) ++m_nVisibleLines;
	return pLayout;
}

/* CLayoutを作成する
	@@@ 2002.09.23 YAZAKI
	@date 2009.08.28 nasukoji	レイアウト長を引数に追加
*/
CLayout* CLayoutMgr::CreateLayout(
	CDocLine*		pCDocLine,
	CLogicPoint		ptLogicPos,
	CLogicInt		nLength,
	EColorIndexType	nTypePrev,
	CLayoutInt		nIndent,
	CLayoutInt		nPosX,
	CLayoutColorInfo*	colorInfo,
	const CBracketNestingState& bracketStateBegin,
	const CBracketNestingState& bracketStateEnd,
	const std::vector<SRainbowBracketMark>& bracketMarks
)
{
	CLayout* pLayout = new (m_layoutMemRes->allocate(sizeof(CLayout))) CLayout(
		pCDocLine,
		ptLogicPos,
		nLength,
		nTypePrev,
		nIndent,
		colorInfo,
		bracketStateBegin,
		bracketStateEnd,
		bracketMarks
	);

	if( pCDocLine->GetEol().IsNone() ){
		pLayout->m_cEol.SetType( EEolType::none );/* 改行コードの種類 */
	}else{
		if( pLayout->GetLogicOffset() + pLayout->GetLengthWithEOL() >
			pCDocLine->GetLengthWithEOL() - pCDocLine->GetEol().GetLen()
		){
			pLayout->m_cEol = pCDocLine->GetEol();/* 改行コードの種類 */
		}else{
			pLayout->m_cEol = EEolType::none;/* 改行コードの種類 */
		}
	}

	// 2009.08.28 nasukoji	「折り返さない」選択時のみレイアウト長を記憶する
	// 「折り返さない」以外で計算しないのはパフォーマンス低下を防ぐ目的なので、
	// パフォーマンスの低下が気にならない程なら全ての折り返し方法で計算する
	// ようにしても良いと思う。
	// （その場合CLayoutMgr::CalculateTextWidth()の呼び出し箇所をチェック）
	pLayout->SetLayoutWidth( ( m_pcEditDoc->m_nTextWrapMethodCur == WRAP_NO_TEXT_WRAP ) ? nPosX : CLayoutInt(0) );

	return pLayout;
}

/*
|| 指定された物理行のデータへのポインタとその長さを返す Ver0

	@date 2002/2/10 aroka CMemory変更
*/
const wchar_t* CLayoutMgr::GetLineStr( CLayoutInt nLine, CLogicInt* pnLineLen ) const //#####いらんやろ
{
	const CLayout* pLayout;
	if( nullptr == ( pLayout = SearchLineByLayoutY( nLine )	) ){
		return nullptr;
	}
	*pnLineLen = CLogicInt(pLayout->GetLengthWithEOL());
	return pLayout->GetDocLineRef()->GetPtr() + pLayout->GetLogicOffset();
}

/*!	指定された物理行のデータへのポインタとその長さを返す Ver1
	@date 2002/03/24 YAZAKI GetLineStr( int nLine, int* pnLineLen )と同じ動作に変更。
*/
const wchar_t* CLayoutMgr::GetLineStr( CLayoutInt nLine, CLogicInt* pnLineLen, const CLayout** ppcLayoutDes ) const
{
	if( nullptr == ( (*ppcLayoutDes) = SearchLineByLayoutY( nLine )	) ){
		return nullptr;
	}
	*pnLineLen = (*ppcLayoutDes)->GetLengthWithEOL();
	return (*ppcLayoutDes)->m_pCDocLine->GetPtr() + (*ppcLayoutDes)->GetLogicOffset();
}

/*
|| 指定された位置がレイアウト行の途中の行末かどうか調べる

	@date 2002/4/27 MIK
*/
bool CLayoutMgr::IsEndOfLine(
	const CLayoutPoint& ptLinePos
)
{
	const CLayout* pLayout;

	if( nullptr == ( pLayout = SearchLineByLayoutY( ptLinePos.GetY2() )	) )
	{
		return false;
	}

	if( pLayout->GetLayoutEol().IsNone() )
	{	/* この行に改行はない */
		/* この行の最後か？ */
		if( ptLinePos.x == (Int)pLayout->GetLengthWithEOL() ) return true; //$$ 単位混在
	}

	return false;
}

/*!	@brief ファイル末尾のレイアウト位置を取得する

	ファイル末尾まで選択する場合に正確な位置情報を与えるため

	既存の関数では物理行からレイアウト位置を変換する必要があり，
	処理に無駄が多いため，専用関数を作成
	
	@date 2006.07.29 genta
	@date 2006.10.01 Moca メンバで保持するように。データ変更時には、_DoLayout/DoLayout_Rangeで無効にする。
*/
void CLayoutMgr::GetEndLayoutPos(
	CLayoutPoint* ptLayoutEnd //[out]
)
{
	if( -1 != m_nEOFLine ){
		ptLayoutEnd->x = m_nEOFColumn;
		ptLayoutEnd->y = m_nEOFLine;
		return;
	}

	if( CLayoutInt(0) == m_nLines || m_pLayoutBot == nullptr ){
		// データが空
		ptLayoutEnd->x = CLayoutInt(0);
		ptLayoutEnd->y = CLayoutInt(0);
		m_nEOFColumn = ptLayoutEnd->x;
		m_nEOFLine = ptLayoutEnd->y;
		return;
	}
	// Ctrl+End / 全選択の終端は必ず実ファイル末尾へ到達させる。
	// 末尾が折りたたみ範囲内なら先に展開して、可視行座標と物理末尾を一致させる。
	if (m_pcDocLineMgr && m_pcDocLineMgr->GetLineCount() > CLogicInt(0)) {
		ExpandCodeFoldsContaining(m_pcDocLineMgr->GetLineCount() - CLogicInt(1));
	}

	CLayout *btm = m_pLayoutBot;
	if( btm->m_cEol.IsValid() ){
		//	末尾に改行がある
		ptLayoutEnd->Set(CLayoutInt(0), GetLineCount());
	}
	else {
		CMemoryIterator it = CreateCMemoryIterator(btm);
		while( !it.end() ){
			it.scanNext();
			it.addDelta();
		}
		ptLayoutEnd->Set(it.getColumn(), GetLineCount() - CLayoutInt(1));
		// [EOF]のみ折り返すのはやめる	// 2009.02.17 ryoji
		//// 2006.10.01 Moca Start [EOF]のみのレイアウト行処理が抜けていたバグを修正
		//if( GetMaxLineKetas() <= ptLayoutEnd->GetX2() ){
		//	ptLayoutEnd->SetX(CLayoutInt(0));
		//	ptLayoutEnd->y++;
		//}
		//// 2006.10.01 Moca End
	}
	m_nEOFColumn = ptLayoutEnd->x;
	m_nEOFLine = ptLayoutEnd->y;
}

/* 論理行の指定範囲に該当するレイアウト情報を削除して */
/* 削除した範囲の直前のレイアウト情報のポインタを返す */
CLayout* CLayoutMgr::DeleteLayoutAsLogical(
	CLayout*	pLayoutInThisArea,
	CLayoutInt	nLineOf_pLayoutInThisArea,
	CLogicInt	nLineFrom,
	CLogicInt	nLineTo,
	CLogicPoint	ptDelLogicalFrom,
	CLayoutInt*	pnDeleteLines
)
{
	CLayout* pLayout;
	CLayout* pLayoutWork;
	CLayout* pLayoutNext;

	*pnDeleteLines = CLayoutInt(0);
	if( CLayoutInt(0) == m_nLines){	/* 全物理行数 */
		return nullptr;
	}
	if( nullptr == pLayoutInThisArea ){
		return nullptr;
	}

	// 1999.11.22
	m_pLayoutPrevRefer = pLayoutInThisArea->GetPrevLayout();
	m_nPrevReferLine = nLineOf_pLayoutInThisArea - CLayoutInt(1);

	/* 範囲内先頭に該当するレイアウト情報をサーチ */
	pLayoutWork = pLayoutInThisArea->GetPrevLayout();
	while( nullptr != pLayoutWork && nLineFrom <= pLayoutWork->GetLogicLineNo()){
		pLayoutWork = pLayoutWork->GetPrevLayout();
	}

	if( nullptr == pLayoutWork ){
		pLayout	= m_pLayoutTop;
	}else{
		pLayout = pLayoutWork->GetNextLayout();
	}
	while( nullptr != pLayout ){
		if( pLayout->GetLogicLineNo() > nLineTo ){
			break;
		}
		pLayoutNext = pLayout->GetNextLayout();
		if( nullptr == pLayoutWork ){
			/* 先頭行の処理 */
			m_pLayoutTop = pLayout->GetNextLayout();
			if( nullptr != pLayout->GetNextLayout() ){
				pLayout->m_pNext->m_pPrev = nullptr;
			}
		}else{
			pLayoutWork->m_pNext = pLayout->GetNextLayout();
			if( nullptr != pLayout->GetNextLayout() ){
				pLayout->m_pNext->m_pPrev = pLayoutWork;
			}
		}
//		if( m_pLayoutPrevRefer == pLayout ){
//			// 1999.12.22 前にずらすだけでよいのでは
//			m_pLayoutPrevRefer = pLayout->GetPrevLayout();
//			--m_nPrevReferLine;
//		}

		if( ( ptDelLogicalFrom.GetY2() == pLayout->GetLogicLineNo() &&
			  ptDelLogicalFrom.GetX2() < pLayout->GetLogicOffset() + pLayout->GetLengthWithEOL() ) ||
			( ptDelLogicalFrom.GetY2() < pLayout->GetLogicLineNo() )
		){
			(*pnDeleteLines)++;
		}

		if( m_pLayoutPrevRefer == pLayout ){
			DEBUG_TRACE( L"バグバグ\n" );
		}

		const bool layoutWasVisible = !IsDisplayHiddenLine(pLayout->GetLogicLineNo());
		pLayout->~CLayout();
		m_layoutMemRes->deallocate(pLayout, sizeof(CLayout), alignof(CLayout));

		m_nLines--;	/* 全物理行数 */
		if (layoutWasVisible) --m_nVisibleLines;
		if( nullptr == pLayoutNext ){
			m_pLayoutBot = pLayoutWork;
		}
		pLayout = pLayoutNext;
	}
//	MYTRACE( L"(*pnDeleteLines)=%d\n", (*pnDeleteLines) );

	return pLayoutWork;
}

/* 指定行より後の行のレイアウト情報について、論理行番号を指定行数だけシフトする */
/* 論理行が削除された場合は０より小さい行数 */
/* 論理行が挿入された場合は０より大きい行数 */
void CLayoutMgr::ShiftLogicalLineNum( CLayout* pLayoutPrev, CLogicInt nShiftLines )
{
	MY_RUNNINGTIMER( cRunningTimer, L"CLayoutMgr::ShiftLogicalLineNum" );

	CLayout* pLayout;
	if( 0 == nShiftLines ){
		return;
	}
	if( nullptr == pLayoutPrev ){
		pLayout = m_pLayoutTop;
	}else{
		pLayout = pLayoutPrev->GetNextLayout();
	}
	/* レイアウト情報全体を更新する(なな、なんと!!!) */
	while( nullptr != pLayout ){
		pLayout->OffsetLogicLineNo(nShiftLines);	/* 対応する論理行番号 */
		pLayout = pLayout->GetNextLayout();
	}
	return;
}

bool CLayoutMgr::ChangeLayoutParam(
	CKetaXInt	nTabSize,
	int			nTsvMode,
	CKetaXInt	nMaxLineKetas
)
{
	if( nTabSize < 1 || nTabSize > 64 ) { return false; }
	if( nMaxLineKetas < MINLINEKETAS || nMaxLineKetas > MAXLINEKETAS ){ return false; }

	m_nTabSpace = nTabSize;
	int nTsvModeOld = m_tsvInfo.m_nTsvMode;
	m_tsvInfo.m_nTsvMode = nTsvMode;
	if (nTsvModeOld != nTsvMode) {
		m_tsvInfo.CalcTabLength(this->m_pcDocLineMgr);
	}
	m_nMaxLineKetas = nMaxLineKetas;

	_DoLayout(true);

	return true;
}

/* 現在位置の単語の範囲を調べる */
bool CLayoutMgr::WhereCurrentWord(
	CLayoutInt		nLineNum,
	CLogicInt		nIdx,
	CLayoutRange*	pSelect,		//!< [out]
	CNativeW*		pcmcmWord,		//!< [out]
	CNativeW*		pcmcmWordLeft,	//!< [out]
	bool expandAddedWordCharacters
)
{
	const CLayout* pLayout = SearchLineByLayoutY( nLineNum );
	if( nullptr == pLayout ){
		return false;
	}

	// 現在位置の単語の範囲を調べる -> ロジック単位pSelect, pcmemWord, pcmemWordLeft
	CLogicInt nFromX;
	CLogicInt nToX;
	bool nRetCode = CSearchAgent(m_pcDocLineMgr).WhereCurrentWord(
		pLayout->GetLogicLineNo(),
		pLayout->GetLogicOffset() + CLogicInt(nIdx),
		&nFromX,
		&nToX,
		pcmcmWord,
		pcmcmWordLeft
	);
	if (nRetCode && expandAddedWordCharacters && !CWordMarker::Instance().AddedWordCharacters().empty()) {
		const CDocLine* docLine = pLayout->GetDocLineRef();
		CLogicInt lineLength;
		const wchar_t* line = docLine->GetDocLineStrWithEOL(&lineLength);
		int from = nFromX;
		int to = nToX;
		if (CWordMarker::Instance().ExpandWordRange(line, lineLength, &from, &to)) {
			nFromX = CLogicInt(from);
			nToX = CLogicInt(to);
			if (pcmcmWord) pcmcmWord->SetString(line + from, to - from);
			if (pcmcmWordLeft) pcmcmWordLeft->SetString(line + from, static_cast<int>(nIdx) - from);
		}
	}

	if( nRetCode ){
		/* 論理位置→レイアウト位置変換 */
		CLayoutPoint ptFrom;
		LogicToLayout( CLogicPoint(nFromX, pLayout->GetLogicLineNo()), &ptFrom, nLineNum );
		pSelect->SetFrom(ptFrom);

		CLayoutPoint ptTo;
		LogicToLayout( CLogicPoint(nToX, pLayout->GetLogicLineNo()), &ptTo, nLineNum );
		pSelect->SetTo(ptTo);
	}
	return nRetCode;
}

/* 現在位置の左右の単語の先頭位置を調べる */
int CLayoutMgr::PrevOrNextWord(
	CLayoutInt		nLineNum,
	CLogicInt		nIdx,
	CLayoutPoint*	pptLayoutNew,
	BOOL			bLEFT,
	BOOL			bStopsBothEnds
)
{
	const CLayout*	pLayout = SearchLineByLayoutY( nLineNum );
	if( nullptr == pLayout ){
		return FALSE;
	}

	// 現在位置の左右の単語の先頭位置を調べる
	CLogicInt	nPosNew;
	int			nRetCode = CSearchAgent(m_pcDocLineMgr).PrevOrNextWord(
		pLayout->GetLogicLineNo(),
		pLayout->GetLogicOffset() + nIdx,
		&nPosNew,
		bLEFT,
		bStopsBothEnds
	);

	if( nRetCode ){
		/* 論理位置→レイアウト位置変換 */
		LogicToLayout(
			CLogicPoint(nPosNew,pLayout->GetLogicLineNo()),
			pptLayoutNew,
			nLineNum
		);
	}
	return nRetCode;
}

//! 単語検索
/*
	@retval 0 見つからない
*/
int CLayoutMgr::SearchWord(
	CLayoutInt				nLine,				//!< [in] 検索開始レイアウト行
	CLogicInt				nIdx,				//!< [in] 検索開始データ位置
	ESearchDirection		eSearchDirection,	//!< [in] 検索方向
	CLayoutRange*			pMatchRange,		//!< [out] マッチレイアウト範囲
	const CSearchStringPattern&	pattern
)
{
	int			nRetCode;
	const CLayout*	pLayout;
	pLayout = this->SearchLineByLayoutY( nLine );
	if( nullptr == pLayout ){
		return FALSE;
	}

	// 単語検索 -> cLogicRange (データ位置)
	CLogicRange cLogicRange;
	nRetCode = CSearchAgent(m_pcDocLineMgr).SearchWord(
		CLogicPoint(pLayout->GetLogicOffset() + nIdx, pLayout->GetLogicLineNo()),
		eSearchDirection,
		&cLogicRange, //pMatchRange,
		pattern
	);

	// 論理位置→レイアウト位置変換
	// cLogicRange -> pMatchRange
	if( nRetCode ){
		LogicToLayout(
			cLogicRange,
			pMatchRange
		);
	}
	return nRetCode;
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                        単位の変換                           //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

/*!
	@brief カーソル位置変換 物理→レイアウト

	物理位置(行頭からのバイト数、折り返し無し行位置)
	→レイアウト位置(行頭からの表示桁位置、折り返しあり行位置)

	@param[in]  ptLogic   ロジック位置
	@param[out] pptLayout レイアウト位置
	@param[in]  nLineHint レイアウトY値のヒント。求める値に近い値を渡すと高速に検索できる。

	@date 2004.06.16 Moca インデント表示の際のTABを含む行の座標ずれ修正
	@date 2007.09.06 kobake 関数名をCaretPos_Phys2LogからLogicToLayoutに変更
*/
void CLayoutMgr::LogicToLayout(
	const CLogicPoint&	ptLogic,
	CLayoutPoint*		pptLayout,
	CLayoutInt			nLineHint,
	bool			expandHiddenLine
)
{
	pptLayout->Clear();

	if(GetLineCount()==0)return; //変換不可
	if (IsCodeFoldHiddenLine(ptLogic.y)) {
		if (expandHiddenLine) {
			ExpandCodeFoldsContaining(ptLogic.y);
		} else if (const auto* collapsed = FindCollapsedCodeFoldContainingLine(ptLogic.y)) {
			// Batch marker scans map hidden lines to a visible fold header without expanding it.
			LogicToLayout(CLogicPoint(CLogicInt(0), CLogicInt(static_cast<Int>(collapsed->firstLine))), pptLayout, nLineHint, false);
			return;
		}
	}
	if (m_bRealtimeFilterActive && !IsCodeFoldHiddenLine(ptLogic.y) && IsDisplayHiddenLine(ptLogic.y)) {
		const CLogicInt logicalLineCount = m_pcDocLineMgr ? m_pcDocLineMgr->GetLineCount() : CLogicInt(0);
		for (CLogicInt line = ptLogic.y + CLogicInt(1); line < logicalLineCount; ++line) {
			if (!IsDisplayHiddenLine(line)) {
				LogicToLayout(CLogicPoint(CLogicInt(0), line), pptLayout, nLineHint, false);
				return;
			}
		}
		for (CLogicInt line = ptLogic.y - CLogicInt(1); line >= CLogicInt(0); --line) {
			if (!IsDisplayHiddenLine(line)) {
				LogicToLayout(CLogicPoint(CLogicInt(0), line), pptLayout, nLineHint, false);
				return;
			}
		}
	}

	// サーチ開始地点 -> pLayout, nCaretPosX, nCaretPosY
	CLayoutInt		nCaretPosX = CLayoutInt(0);
	CLayoutInt		nCaretPosY;
	const CLayout*	pLayout;
	if (IsCodeFoldActive() || IsRealtimeFilterActive()) {
		// 論理行番号と表示行番号は折りたたみ後に一致しない。
		// 対象行が隠れている場合は展開し、先頭から表示行番号を数えて正しい位置を得る。
		ExpandCodeFoldsContaining(ptLogic.y);
		nCaretPosY = CLayoutInt(0);
		pLayout = m_pLayoutTop;
		while (pLayout && pLayout->GetLogicLineNo() < ptLogic.y) {
			if (!IsDisplayHiddenLine(pLayout->GetLogicLineNo())) ++nCaretPosY;
			pLayout = pLayout->GetNextLayout();
		}
		if (!pLayout) {
			pptLayout->SetY(GetLineCount());
			return;
		}
		while (pLayout->GetNextLayout()
			&& pLayout->GetNextLayout()->GetLogicLineNo() == ptLogic.y
			&& pLayout->GetNextLayout()->GetLogicOffset() <= ptLogic.x) {
			pLayout = pLayout->GetNextLayout();
			if (!IsDisplayHiddenLine(pLayout->GetLogicLineNo())) ++nCaretPosY;
		}
	} else {
		// 2013.05.15 ヒント、ありなしの処理を統合
		nLineHint = t_min(GetLineCount() - 1, nLineHint);
		nCaretPosY = t_max(CLayoutInt(ptLogic.y), nLineHint);

		// 2013.05.12 m_pLayoutPrevReferを見る
		if( nCaretPosY <= m_nPrevReferLine && m_pLayoutPrevRefer
			&& m_pLayoutPrevRefer->GetLogicLineNo() <= ptLogic.y ){
			// ヒントより現在位置のほうが後ろか同じぐらいで近い
			nCaretPosY = CLayoutInt(ptLogic.y - m_pLayoutPrevRefer->GetLogicLineNo()) + m_nPrevReferLine;
			pLayout = SearchLineByLayoutY(nCaretPosY);
		}else{
			pLayout = SearchLineByLayoutY(nCaretPosY);
		}
		if( !pLayout ){
			pptLayout->SetY( m_nLines );
			return;
		}

		//ロジックYがでかすぎる場合は、一致するまでデクリメント (
		while(pLayout->GetLogicLineNo() > ptLogic.GetY2()){
			pLayout = pLayout->GetPrevLayout();
			nCaretPosY--;
		}

		//ロジックYが同じでOffsetが行き過ぎている場合は戻る
		if(pLayout->GetLogicLineNo() == ptLogic.GetY2()){
			while( pLayout && pLayout->GetPrevLayout() && pLayout->GetPrevLayout()->GetLogicLineNo() == ptLogic.GetY2()
				&& ptLogic.x < pLayout->GetLogicOffset() ){
				pLayout = pLayout->GetPrevLayout();
				nCaretPosY--;
			}
		}
	}

	//	Layoutを１つずつ先に進めながらptLogic.yが物理行に一致するLayoutを探す
	while( pLayout ){
		if( ptLogic.GetY2() == pLayout->GetLogicLineNo() ){
			// 2013.05.10 Moca 高速化
			const CLayout* pLayoutNext = pLayout->GetNextLayout();
			if( pLayoutNext && ptLogic.GetY2() ==pLayoutNext->GetLogicLineNo()
					&& pLayoutNext->GetLogicOffset() <= ptLogic.x ){
				nCaretPosY++;
				pLayout = pLayout->GetNextLayout();
				continue;
			}

			//	2004.06.16 Moca インデント表示の際に位置がずれる(TAB位置ずれによる)
			//	TAB幅を正確に計算するには当初からインデント分を加えておく必要がある．
			nCaretPosX = pLayout->GetIndent();
			const wchar_t*	pData = pLayout->GetDocLineRef()->GetPtr() + pLayout->GetLogicOffset(); // 2002/2/10 aroka CMemory変更
			CLogicInt	nDataLen = (CLogicInt)pLayout->GetLengthWithEOL();

			CLogicInt i;
			for( i = CLogicInt(0); i < nDataLen; ++i ){
				if( pLayout->GetLogicOffset() + i >= ptLogic.x ){
					break;
				}

				//文字ロジック幅 -> nCharChars
				CLogicInt nCharChars = CNativeW::GetSizeOfChar( pData, nDataLen, i );
				if( nCharChars == 0 )
					nCharChars = CLogicInt(1);

				//文字レイアウト幅 -> nCharKetas
				CLayoutInt nCharKetas;
				if( pData[i] ==	WCODE::TAB || ( pData[i] == L',' && m_tsvInfo.m_nTsvMode == TSV_MODE_CSV) ){
					nCharKetas = GetActualTsvSpace( nCaretPosX, pData[i] );
				}
				else{
					nCharKetas = GetLayoutXOfChar( pData, nDataLen, i );
				}

				//レイアウト加算
				nCaretPosX += nCharKetas;

				//ロジック加算
				if( pData[i] ==	WCODE::TAB ){
					nCharChars = CLogicInt(1);
				}
				i += nCharChars - CLogicInt(1);
			}
			if( i < nDataLen ){
				//	ptLogic.x, ptLogic.yがこの行の中に見つかったらループ打ち切り
				break;
			}

			if( !pLayout->GetNextLayout() ){
				//	当該位置に達していなくても，レイアウト末尾ならデータ末尾のレイアウト位置を返す．
				nCaretPosX = pLayout->CalcLayoutWidth(*this) + CLayoutInt(pLayout->GetLayoutEol().GetLen()>0?1+m_nSpacing:0);
				break;
			}

			if( ptLogic.y < pLayout->m_pNext->GetLogicLineNo() ){
				//	次のLayoutが当該物理行を過ぎてしまう場合はデータ末尾のレイアウト位置を返す．
				nCaretPosX = pLayout->CalcLayoutWidth(*this) + CLayoutInt(pLayout->GetLayoutEol().GetLen()>0?1+m_nSpacing:0);
				break;
			}
		}
		if( ptLogic.GetY2() < pLayout->GetLogicLineNo() ){
			//	ふつうはここには来ないと思うが... (genta)
			//	Layoutの指す物理行が探している行より先を指していたら打ち切り
			break;
		}

		//	次の行へ進む
		nCaretPosY++;
		pLayout = pLayout->GetNextLayout();
	}

	//	2004.06.16 Moca インデント表示の際の位置ずれ修正
	pptLayout->Set(
		pLayout ? nCaretPosX : CLayoutInt(0),
		nCaretPosY
	);
}

/*!
	@brief カーソル位置変換  レイアウト→物理

	レイアウト位置(行頭からの表示桁位置、折り返しあり行位置)
	→物理位置(行頭からのバイト数、折り返し無し行位置)

	@date 2007.09.06 kobake 関数名をCaretPos_Log2Phys→LayoutToLogicに変更
*/
void CLayoutMgr::LayoutToLogicEx(
	const CLayoutPoint&	ptLayout,	//!< [in]  レイアウト位置
	CLogicPointEx*		pptLogic	//!< [out] ロジック位置
) const
{
	pptLogic->Set(CLogicInt(0), CLogicInt(0));
	pptLogic->ext = 0;
	pptLogic->haba = m_nCharLayoutXPerKeta;
	if( ptLayout.GetY2() > m_nLines ){
		//2007.10.11 kobake Y値が間違っていたので修正
		//pptLogic->Set(0, m_nLines);
		pptLogic->Set(CLogicInt(0), m_pcDocLineMgr->GetLineCount());
		return;
	}

	CLogicInt		nDataLen;
	const wchar_t*	pData;

	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
	//                        Ｙ値の決定                           //
	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
	BOOL			bEOF = FALSE;
	CLayoutInt		nX;
	const CLayout*	pcLayout = SearchLineByLayoutY( ptLayout.GetY2() );
	if( !pcLayout ){
		if( 0 < ptLayout.y ){
			pcLayout = SearchLineByLayoutY( ptLayout.GetY2() - CLayoutInt(1) );
			if( nullptr == pcLayout ){
				pptLogic->Set(CLogicInt(0), m_pcDocLineMgr->GetLineCount());
				return;
			}
			else{
				pData = GetLineStr( ptLayout.GetY2() - CLayoutInt(1), &nDataLen );
				if( WCODE::IsLineDelimiter(pData[nDataLen - 1], GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol) ){
					pptLogic->Set(CLogicInt(0), m_pcDocLineMgr->GetLineCount());
					return;
				}
				else{
					pptLogic->y = m_pcDocLineMgr->GetLineCount() - 1; // 2002/2/10 aroka CDocLineMgr変更
					bEOF = TRUE;
					// nX = CLayoutInt(MAXLINEKETAS);
					nX = pcLayout->GetIndent();
					goto checkloop;
				}
			}
		}
		//2007.10.11 kobake Y値が間違っていたので修正
		//pptLogic->Set(0, m_nLines);
		pptLogic->Set(CLogicInt(0), m_pcDocLineMgr->GetLineCount());
		return;
	}
	else{
		pptLogic->y = pcLayout->GetLogicLineNo();
	}

	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
	//                        Ｘ値の決定                           //
	// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
	pData = GetLineStr( ptLayout.GetY2(), &nDataLen );
	nX = pcLayout ? pcLayout->GetIndent() : CLayoutInt(0);

checkloop:;
	CLogicInt	i;
	for( i = CLogicInt(0); i < nDataLen; ++i )
	{
		//文字ロジック幅 -> nCharChars
		CLogicInt	nCharChars;
		nCharChars = CNativeW::GetSizeOfChar( pData, nDataLen, i );
		if( nCharChars == 0 )
			nCharChars = CLogicInt(1);
		
		//文字レイアウト幅 -> nCharKetas
		CLayoutInt	nCharKetas;
		if( pData[i] == WCODE::TAB || (pData[i] == L',' && m_tsvInfo.m_nTsvMode == TSV_MODE_CSV ) ){
			nCharKetas = GetActualTsvSpace( nX, pData[i] );
		}
		else{
			nCharKetas = GetLayoutXOfChar( pData, nDataLen, i );
		}

		//レイアウト加算
		if( nX + nCharKetas > ptLayout.GetX2() && !bEOF ){
			break;
		}
		nX += nCharKetas;

		//ロジック加算
		if( pData[i] ==	WCODE::TAB ){
			nCharChars = CLogicInt(1);
		}
		i += nCharChars - CLogicInt(1);
	}
	i += pcLayout->GetLogicOffset();
	pptLogic->x = i;
	pptLogic->ext = ptLayout.GetX2() - nX;
	return;
}

void CLayoutMgr::LayoutToLogic( const CLayoutPoint& ptLayout, CLogicPoint* pptLogic ) const
{
	CLogicPointEx ptEx;
	LayoutToLogicEx( ptLayout, &ptEx );
	*pptLogic = static_cast<CLogicPoint&>(ptEx);
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                         デバッグ                            //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

/* テスト用にレイアウト情報をダンプ */
void CLayoutMgr::DUMP()
{
#ifdef _DEBUG
	const wchar_t* pData;
	CLogicInt nDataLen;
	MYTRACE( L"------------------------\n" );
	MYTRACE( L"m_nLines=%d\n", m_nLines );
	MYTRACE( L"m_pLayoutTop=%08lxh\n", m_pLayoutTop );
	MYTRACE( L"m_pLayoutBot=%08lxh\n", m_pLayoutBot );
	MYTRACE( L"m_nMaxLineKetas=%d\n", m_nMaxLineKetas );

	MYTRACE( L"m_nTabSpace=%d\n", m_nTabSpace );
	CLayout* pLayout;
	CLayout* pLayoutNext;
	pLayout = m_pLayoutTop;
	while( nullptr != pLayout ){
		pLayoutNext = pLayout->GetNextLayout();
		MYTRACE( L"\t-------\n" );
		MYTRACE( L"\tthis=%08lxh\n", pLayout );
		MYTRACE( L"\tm_pPrev =%08lxh\n",		pLayout->GetPrevLayout() );
		MYTRACE( L"\tm_pNext =%08lxh\n",		pLayout->GetNextLayout() );
		MYTRACE( L"\tm_nLinePhysical=%d\n",	pLayout->GetLogicLineNo() );
		MYTRACE( L"\tm_nOffset=%d\n",		pLayout->GetLogicOffset() );
		MYTRACE( L"\tm_nLength=%d\n",		pLayout->GetLengthWithEOL() );
		MYTRACE( L"\tm_enumEOLType =%ls\n",	pLayout->GetLayoutEol().GetName() );
		MYTRACE( L"\tm_nEOLLen =%d\n",		pLayout->GetLayoutEol().GetLen() );
		MYTRACE( L"\tm_nTypePrev=%d\n",		pLayout->GetColorTypePrev() );
		pData = CDocReader(*m_pcDocLineMgr).GetLineStr( pLayout->GetLogicLineNo(), &nDataLen );
		MYTRACE( L"\t[%ls]\n", pData );
		pLayout = pLayoutNext;
	}
	MYTRACE( L"------------------------\n" );
#endif
	return;
}
