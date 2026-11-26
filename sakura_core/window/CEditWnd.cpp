/*!	@file
	@brief 編集ウィンドウ（外枠）管理クラス

	@author Norio Nakatani
*/
/*
	Copyright (C) 1998-2001, Norio Nakatani
	Copyright (C) 2000-2001, genta, jepro, ao
	Copyright (C) 2001, MIK, Stonee, Misaka, hor, YAZAKI
	Copyright (C) 2002, YAZAKI, genta, hor, aroka, minfu, 鬼, MIK, ai
	Copyright (C) 2003, genta, MIK, Moca, wmlhq, ryoji, KEITA
	Copyright (C) 2004, genta, Moca, yasu, MIK, novice, Kazika
	Copyright (C) 2005, genta, MIK, Moca, aroka, ryoji
	Copyright (C) 2006, genta, ryoji, aroka, fon, yukihane
	Copyright (C) 2007, ryoji
	Copyright (C) 2008, ryoji, nasukoji
	Copyright (C) 2009, ryoji, nasukoji, Hidetaka Sakai
	Copyright (C) 2010, ryoji, Moca、Uchi
	Copyright (C) 2011, ryoji
	Copyright (C) 2013, Uchi
	Copyright (C) 2018-2026, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/

#include "StdAfx.h"
#include <ShlObj.h>
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <limits>
#include <string_view>

#include "window/CEditWnd.h"
#include "parse/CSVTableSettings.h"
#include "parse/CSVStatistics.h"
#include "_main/CControlTray.h"
#include "_main/CCommandLine.h"	/// 2003/1/26 aroka
#include "_main/CAppMode.h"
#include "basis/CErrorInfo.h"
#include "dlg/CDlgAbout.h"
#include "dlg/CDlgInput1.h"
#include "dlg/CDlgPrintSetting.h"
#include "env/CShareData.h"
#include "env/CAppNodeManager.h"
#include "env/CSakuraEnvironment.h"
#include "charset/CCodeFactory.h"
#include "charset/CCodeBase.h"
#include "CEditApp.h"
#include "recent/CMRUFile.h"
#include "recent/CMRUFolder.h"
#include "util/module.h"
#include "util/os.h"		//WM_MOUSEWHEEL,WM_THEMECHANGED
#include "util/window.h"
#include "util/shell.h"
#include "util/string_ex2.h"
#include "plugin/CJackManager.h"
#include "agent/CGrepAgent.h"
#include "env/CMarkMgr.h"
#include "doc/layout/CLayout.h"
#include "doc/CEditDoc.h"
#include "doc/logic/CDocLine.h"
#include "view/colors/CWordMarker.h"
#include "config/system_constants.h"
#include "debug/CRunningTimer.h"
#include "apiwrap/StdApi.h"
#include "apiwrap/CommonControl.h"
#include "sakura_rc.h"
#include "config/app_constants.h"
#include "recent/CRecentEditNode.h"
#include "recent/CRecentFile.h"
#include "recent/CRecentFolder.h"
#include "apiwrap/DarkMode.h"

#include "macro/CMacroFactory.h"
#include "view/colors/CColorStrategy.h"
#include "view/figures/CFigureManager.h"

//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたので
//	定義を削除

#define		YOHAKU_X		4		/* ウィンドウ内の枠と紙の隙間最小値 */
#define		YOHAKU_Y		4		/* ウィンドウ内の枠と紙の隙間最小値 */

static constexpr std::size_t CSV_BATCH_EDIT_MAX_SOURCE_CHARS = 8u * 1024u * 1024u;
static constexpr std::size_t CSV_BATCH_EDIT_MAX_RECORDS = 100000u;
static constexpr int IDC_REALTIME_FILTER_INCLUDE_LABEL = 0x7CE0;
static constexpr int IDC_REALTIME_FILTER_EXCLUDE_LABEL = 0x7CE1;
static constexpr int IDC_REALTIME_FILTER_INCLUDE = 0x7CE2;
static constexpr int IDC_REALTIME_FILTER_EXCLUDE = 0x7CE3;
static constexpr int IDC_REALTIME_FILTER_LOGIC = 0x7CE4;
static constexpr int IDC_REALTIME_FILTER_CASE = 0x7CE5;
static constexpr int IDC_REALTIME_FILTER_REGEX = 0x7CE6;
static constexpr int IDC_REALTIME_FILTER_CONTEXT = 0x7CE7;
static constexpr int IDC_REALTIME_FILTER_STATS = 0x7CE8;
static constexpr int IDC_REALTIME_FILTER_COPY = 0x7CE9;
static constexpr int IDC_REALTIME_FILTER_NEW = 0x7CEA;
static constexpr int IDC_REALTIME_FILTER_SAVEAS = 0x7CEB;
static constexpr int IDC_REALTIME_FILTER_CLEAR = 0x7CEC;
static constexpr int IDC_REALTIME_FILTER_CLOSE = 0x7CED;
static constexpr int IDC_REALTIME_FILTER_CONTEXT_LABEL = 0x7CEE;
static constexpr int IDC_REALTIME_FILTER_AUTOSCROLL = 0x7CEF;
static constexpr int IDC_REALTIME_FILTER_CANCEL = 0x7CF0;

static std::size_t GetRealtimeFilterContextRows(HWND combo)
{
	wchar_t text[32]{};
	::GetWindowTextW(combo, text, static_cast<int>(std::size(text)));
	if (text[0] == L'-') return 0;
	wchar_t* end = nullptr;
	const unsigned long long value = std::wcstoull(text, &end, 10);
	if (!end || end == text || *end != L'\0') return 0;
	return static_cast<std::size_t>(std::min<unsigned long long>(value, 1000000ull));
}
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたので
//	定義を削除

//	状況によりメニューの表示を変えるコマンドリスト(SetMenuFuncSelで使用)
//		2010/5/19	Uchi
//		2012/10/19	syat	各国語対応のため定数化
struct SFuncMenuName {
	EFunctionCode	eFunc;
	int				nNameId[2];		// 選択文字列ID
};

static const SFuncMenuName	sFuncMenuName[] = {
	{F_RECKEYMACRO,			{F_RECKEYMACRO_REC,				F_RECKEYMACRO_APPE}},
	{F_SAVEKEYMACRO,		{F_SAVEKEYMACRO_REC,			F_SAVEKEYMACRO_APPE}},
	{F_LOADKEYMACRO,		{F_LOADKEYMACRO_REC,			F_LOADKEYMACRO_APPE}},
	{F_EXECKEYMACRO,		{F_EXECKEYMACRO_REC,			F_EXECKEYMACRO_APPE}},
	{F_SPLIT_V,				{F_SPLIT_V_ON,					F_SPLIT_V_OFF}},
	{F_SPLIT_H,				{F_SPLIT_H_ON,					F_SPLIT_H_OFF}},
	{F_SPLIT_VH,			{F_SPLIT_VH_ON,					F_SPLIT_VH_OFF}},
	{F_TAB_CLOSEOTHER,		{F_TAB_CLOSEOTHER_TAB,			F_TAB_CLOSEOTHER_WINDOW}},
	{F_TOPMOST,				{F_TOPMOST_SET,					F_TOPMOST_REL}},
	{F_BIND_WINDOW,			{F_TAB_GROUPIZE,				F_TAB_GROUPDEL}},
	{F_SHOWTOOLBAR,			{F_SHOWTOOLBAR_ON,				F_SHOWTOOLBAR_OFF}},
	{F_SHOWFUNCKEY,			{F_SHOWFUNCKEY_ON,				F_SHOWFUNCKEY_OFF}},
	{F_SHOWTAB,				{F_SHOWTAB_ON,					F_SHOWTAB_OFF}},
	{F_SHOWSTATUSBAR,		{F_SHOWSTATUSBAR_ON,			F_SHOWSTATUSBAR_OFF}},
	{F_SHOWMINIMAP,			{F_SHOWMINIMAP_ON,				F_SHOWMINIMAP_OFF}},
	{F_TOGGLE_KEY_SEARCH,	{F_TOGGLE_KEY_SEARCH_ON,		F_TOGGLE_KEY_SEARCH_OFF}},
};

static void ShowCodeBox( HWND hWnd, CEditDoc* pcEditDoc )
{
	// カーソル位置の文字列を取得
	const CLayout*	pcLayout;
	CLogicInt		nLineLen;
	const CEditView* pcView = &GetEditWnd().GetActiveView();
	const CCaret* pcCaret = &pcView->GetCaret();
	const CLayoutMgr* pLayoutMgr = &pcEditDoc->m_cLayoutMgr;
	const wchar_t*	pLine = pLayoutMgr->GetLineStr( pcCaret->GetCaretLayoutPos().GetY2(), &nLineLen, &pcLayout );

	// -- -- -- -- キャレット位置の文字情報 -> szCaretChar -- -- -- -- //
	//
	if( pLine ){
		// 指定された桁に対応する行のデータ内の位置を調べる
		CLogicInt nIdx = pcView->LineColumnToIndex( pcLayout, pcCaret->GetCaretLayoutPos().GetX2() );
		if( nIdx < nLineLen ){
			if( nIdx < nLineLen - (pcLayout->GetLayoutEol().GetLen()?1:0) ){
				// 一時的に表示方法の設定を変更する
				CommonSetting_Statusbar sStatusbar;
				sStatusbar.m_bDispUniInSjis		= false;
				sStatusbar.m_bDispUniInJis		= false;
				sStatusbar.m_bDispUniInEuc		= false;
				sStatusbar.m_bDispUtf8Codepoint	= false;
				sStatusbar.m_bDispSPCodepoint	= false;

				WCHAR szMsg[128];
				WCHAR szCode[CODE_CODEMAX][32];
				wchar_t szChar[3];
				int nCharChars = CNativeW::GetSizeOfChar( pLine, nLineLen, nIdx );
				memcpy(szChar, &pLine[nIdx], nCharChars * sizeof(wchar_t));
				szChar[nCharChars] = L'\0';
				for( int i = 0; i < CODE_CODEMAX; i++ ){
					if( i == CODE_SJIS || i == CODE_JIS || i == CODE_EUC || i == CODE_LATIN1 || i == CODE_UNICODE || i == CODE_UTF8 || i == CODE_CESU8 ){
						//auto_sprintf( szCaretChar, L"%04x", );
						//任意の文字コードからUnicodeへ変換する		2008/6/9 Uchi
						CCodeBase* pCode = CCodeFactory::CreateCodeBase((ECodeType)i, false);
						EConvertResult ret = pCode->UnicodeToHex(&pLine[nIdx], nLineLen - nIdx, szCode[i], &sStatusbar);
						delete pCode;
						if (ret != RESULT_COMPLETE) {
							// うまくコードが取れなかった
							wcscpy(szCode[i], L"-");
						}
					}
				}
				// コードポイント部（サロゲートペアも）
				WCHAR szCodeCP[32];
				sStatusbar.m_bDispSPCodepoint = true;
				CCodeBase* pCode = CCodeFactory::CreateCodeBase(CODE_UNICODE, false);
				EConvertResult ret = pCode->UnicodeToHex(&pLine[nIdx], nLineLen - nIdx, szCodeCP, &sStatusbar);
				delete pCode;
				if (ret != RESULT_COMPLETE) {
					// うまくコードが取れなかった
					wcscpy(szCodeCP, L"-");
				}

				// メッセージボックス表示
				auto_sprintf(szMsg, LS(STR_ERR_DLGEDITWND13),
					szChar, szCodeCP, szCode[CODE_SJIS], szCode[CODE_JIS], szCode[CODE_EUC], szCode[CODE_LATIN1], szCode[CODE_UNICODE], szCode[CODE_UTF8], szCode[CODE_CESU8]);
				::MessageBox( hWnd, szMsg, GSTR_APPNAME, MB_OK );
			}
		}
	}
}

/*!
 * 編集ウインドウのアドレスを取得します。
 */
CEditWnd* GetEditWndPtr() noexcept
{
	return CEditWnd::getInstance();
}

/*!
 * 編集ウインドウの参照を取得します。
 *
 * 編集ウインドウの生存期間ははエディタプロセスと同じなので、
 * ほとんどの場合、このグローバル関数を使ってアクセスできます。
 *
 * @throws CEditWndが生成されていない
 */
CEditWnd& GetEditWnd()
{
	auto pcEditWnd = GetEditWndPtr();
	if (!pcEditWnd) {
		throw std::domain_error("CEditWnd is not initialized");
	}
	return *pcEditWnd;
}

CViewFont* GetViewFont(bool isMiniMap)
{
	return GetEditWnd().GetViewFont(isMiniMap);
}

//	/* メッセージループ */
//	DWORD MessageLoop_Thread( DWORD pCEditWndObject );

LRESULT CALLBACK CEditWndProc(
	HWND	hwnd,	// handle of window
	UINT	uMsg,	// message identifier
	WPARAM	wParam,	// first message parameter
	LPARAM	lParam 	// second message parameter
)
{
	CEditWnd* pcWnd = ( CEditWnd* )::GetWindowLongPtr( hwnd, GWLP_USERDATA );
	if( pcWnd ){
		return pcWnd->DispatchEvent( hwnd, uMsg, wParam, lParam );
	}
	return ::DefWindowProc( hwnd, uMsg, wParam, lParam );
}

CEditWnd::CEditWnd()
{
	const auto& csvSettings = csvtable::GetUserSettings();
	m_chCSVDelimiter = csvSettings.delimiter;
	m_bCSVAutoDetectDelimiter = csvSettings.autoDetectDelimiter;
	m_bCSVHasHeader = csvSettings.hasHeader;
	m_nCurlSyntaxErrorListHeight = curlsyntax::GetErrorListHeight();
	const auto& cTypeConfig = GetEditDoc().m_cDocType.GetDocumentAttribute();
	auto& cLayoutMgr = GetEditDoc().m_cLayoutMgr;
	cLayoutMgr.SetLayoutInfo( true, false, cTypeConfig,
		cLayoutMgr.GetTabSpaceKetas(), cLayoutMgr.m_tsvInfo.m_nTsvMode,
		cLayoutMgr.GetMaxLineKetas(), CLayoutXInt(-1), &GetLogfont() );

	// [0] - [3] まで作成・初期化していたものを[0]だけ作る。ほかは分割されるまで何もしない
	m_pcEditViewArr[0] = std::make_unique<CEditView>();
	m_pcEditView = m_pcEditViewArr[0].get();
}

CEditWnd::~CEditWnd()
{
	::KillTimer(m_hWnd, IDT_REALTIME_FILTER_DEBOUNCE);
	::KillTimer(m_hWnd, IDT_REALTIME_FILTER_SCAN);
	if (m_pRealtimeFilterService) {
		m_pRealtimeFilterService->Cancel();
		m_pRealtimeFilterService.reset();
	}
	if (m_hwndCurlSyntaxTooltip) ::DestroyWindow(m_hwndCurlSyntaxTooltip);
	::KillTimer(m_hWnd, IDT_CURL_SYNTAX);
	if (m_pCurlSyntaxCheckService) m_pCurlSyntaxCheckService->Cancel();
	if (m_pCSVTableParseService) {
		m_pCSVTableParseService->Cancel();
		m_pCSVTableParseService.reset(); // Join before destroying the result mailbox.
	}
	if (m_pCSVTableQueryService) {
		m_pCSVTableQueryService->Cancel();
		m_pCSVTableQueryService.reset();
	}
	if (m_pCSVValidationService) {
		m_pCSVValidationService->Cancel();
		m_pCSVValidationService.reset();
	}
	{
		std::lock_guard<std::mutex> lock(m_curlSyntaxMailboxMutex);
		m_curlSyntaxMailbox.reset();
	}
	{
		std::lock_guard<std::mutex> lock(m_csvTableMailboxMutex);
		m_csvTableMailbox.reset();
	}
	{
		std::lock_guard<std::mutex> lock(m_csvTableQueryMailboxMutex);
		m_csvTableQueryMailbox.reset();
	}
	{
		std::lock_guard<std::mutex> lock(m_csvValidationMailboxMutex);
		m_csvValidationMailbox.reset();
	}
	CMacroFactory::resetInstance();
	CColorStrategyPool::resetInstance();
	CFigureManager::resetInstance();

	delete[] m_pszLastCaption;

	m_hWnd = nullptr;
}

bool CEditWnd::IsCurlSyntaxDocument() const
{
	const CEditDoc* document = GetDocument();
	if (!document) return false;
	std::wstring typeName = document->m_cDocType.GetDocumentAttribute().m_szTypeName;
	std::transform(typeName.begin(), typeName.end(), typeName.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
	if (typeName.find(L"curl") != std::wstring::npos) return true;
	const LPCWSTR extension = document->m_cDocFile.GetFilePathClass().GetExt(true);
	return extension && (_wcsicmp(extension, L"scurl") == 0
		|| _wcsicmp(extension, L"dcurl") == 0
		|| _wcsicmp(extension, L"curl") == 0
		|| _wcsicmp(extension, L"mcurl") == 0
		|| _wcsicmp(extension, L"xcurl") == 0);
}

void CEditWnd::NotifyCurlSyntaxContentChanged(CEditDoc* document)
{
	if (!document) return;
	++document->m_uCurlSyntaxVersion;
	document->m_bCurlSyntaxResultStale = true;
	document->m_vCurlSyntaxDiagnostics.clear();
	if (document == GetDocument() && (m_bCSVValidationListActive || m_bCSVValidationHasResult)) {
		m_bCSVValidationStale = true;
		m_uCSVValidationPendingOffset = static_cast<std::size_t>(-1);
		if (m_pCSVValidationService) m_pCSVValidationService->Cancel();
	}
	RefreshCurlSyntaxErrorList();
	if (document != GetDocument()) return;
	if (m_bCSVTableMode) ScheduleCSVTableParse();
	if (m_bRealtimeFilterMode && !m_bRealtimeFilterDocumentLoading) ScheduleRealtimeFilter();
	HideCurlSyntaxTooltip();
	::KillTimer(GetHwnd(), IDT_CURL_SYNTAX);
	if (m_pCurlSyntaxCheckService) m_pCurlSyntaxCheckService->Cancel();
	if (curlsyntax::IsRealtimeEnabled() && IsCurlSyntaxDocument() && GetHwnd()) {
		::SetTimer(GetHwnd(), IDT_CURL_SYNTAX, 500, nullptr);
	}
}

void CEditWnd::ToggleRealtimeFilter()
{
	if (m_bRealtimeFilterMode) {
		CloseRealtimeFilter();
		return;
	}
	if (m_bCSVTableMode) {
		SendStatusMessage(L"CSV テーブル表示を終了してからフィルターを開いてください。");
		return;
	}
	if (!GetDocument() || !m_cSplitterWnd.GetHwnd()) return;
	if (!m_hwndRealtimeFilterBar) {
		auto destroyFilterControls = [this]() {
			for (int id = IDC_REALTIME_FILTER_INCLUDE_LABEL; id <= IDC_REALTIME_FILTER_CANCEL; ++id) {
				if (HWND control = ::GetDlgItem(GetHwnd(), id)) ::DestroyWindow(control);
			}
			if (m_hwndRealtimeFilterBar) ::DestroyWindow(m_hwndRealtimeFilterBar);
			m_hwndRealtimeFilterBar = nullptr;
			m_hwndRealtimeFilterInclude = m_hwndRealtimeFilterExclude = nullptr;
			m_hwndRealtimeFilterLogic = m_hwndRealtimeFilterCase = m_hwndRealtimeFilterRegex = nullptr;
			m_hwndRealtimeFilterContext = m_hwndRealtimeFilterStats = m_hwndRealtimeFilterAutoScroll = nullptr;
		};
		auto createControl = [this](DWORD exStyle, LPCWSTR className, LPCWSTR title,
			DWORD style, int id) -> HWND {
			return ::CreateWindowExW(exStyle, className, title, WS_CHILD | style,
				0, 0, 0, 0, GetHwnd(), reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
				CEditApp::getInstance()->GetAppInstance(), nullptr);
		};
		m_hwndRealtimeFilterBar = createControl(WS_EX_CLIENTEDGE, L"STATIC", L"",
			SS_ETCHEDFRAME, 0x7CDF);
		::CreateWindowExW(0, L"STATIC", L"含む（1行1条件）", WS_CHILD | SS_LEFT,
			0, 0, 0, 0, GetHwnd(), reinterpret_cast<HMENU>(IDC_REALTIME_FILTER_INCLUDE_LABEL),
			CEditApp::getInstance()->GetAppInstance(), nullptr);
		::CreateWindowExW(0, L"STATIC", L"除外（1行1条件）", WS_CHILD | SS_LEFT,
			0, 0, 0, 0, GetHwnd(), reinterpret_cast<HMENU>(IDC_REALTIME_FILTER_EXCLUDE_LABEL),
			CEditApp::getInstance()->GetAppInstance(), nullptr);
		m_hwndRealtimeFilterInclude = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"",
			ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_TABSTOP, IDC_REALTIME_FILTER_INCLUDE);
		m_hwndRealtimeFilterExclude = createControl(WS_EX_CLIENTEDGE, L"EDIT", L"",
			ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_TABSTOP, IDC_REALTIME_FILTER_EXCLUDE);
		m_hwndRealtimeFilterLogic = createControl(0, L"COMBOBOX", L"",
			CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, IDC_REALTIME_FILTER_LOGIC);
		m_hwndRealtimeFilterCase = createControl(0, L"BUTTON", L"大文字と小文字を区別",
			BS_AUTOCHECKBOX | WS_TABSTOP, IDC_REALTIME_FILTER_CASE);
		m_hwndRealtimeFilterRegex = createControl(0, L"BUTTON", L"正規表現",
			BS_AUTOCHECKBOX | WS_TABSTOP, IDC_REALTIME_FILTER_REGEX);
		::CreateWindowExW(0, L"STATIC", L"前後の行", WS_CHILD | SS_LEFT,
			0, 0, 0, 0, GetHwnd(), reinterpret_cast<HMENU>(IDC_REALTIME_FILTER_CONTEXT_LABEL),
			CEditApp::getInstance()->GetAppInstance(), nullptr);
		m_hwndRealtimeFilterContext = createControl(WS_EX_CLIENTEDGE, L"COMBOBOX", L"0",
			CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, IDC_REALTIME_FILTER_CONTEXT);
		m_hwndRealtimeFilterStats = createControl(0, L"STATIC", L"条件を入力してください",
			SS_LEFT | SS_CENTERIMAGE, IDC_REALTIME_FILTER_STATS);
		m_hwndRealtimeFilterAutoScroll = createControl(0, L"BUTTON", L"末尾へ自動スクロール",
			BS_AUTOCHECKBOX | WS_TABSTOP, IDC_REALTIME_FILTER_AUTOSCROLL);
		createControl(0, L"BUTTON", L"表示行をコピー", BS_PUSHBUTTON | WS_TABSTOP, IDC_REALTIME_FILTER_COPY);
		createControl(0, L"BUTTON", L"新規文書へ出力", BS_PUSHBUTTON | WS_TABSTOP, IDC_REALTIME_FILTER_NEW);
		createControl(0, L"BUTTON", L"別名保存", BS_PUSHBUTTON | WS_TABSTOP, IDC_REALTIME_FILTER_SAVEAS);
		createControl(0, L"BUTTON", L"すべて解除", BS_PUSHBUTTON | WS_TABSTOP, IDC_REALTIME_FILTER_CLEAR);
		createControl(0, L"BUTTON", L"処理を中止", BS_PUSHBUTTON | WS_TABSTOP, IDC_REALTIME_FILTER_CANCEL);
		createControl(0, L"BUTTON", L"閉じる", BS_PUSHBUTTON | WS_TABSTOP, IDC_REALTIME_FILTER_CLOSE);
		if (!m_hwndRealtimeFilterBar || !m_hwndRealtimeFilterInclude ||
			!m_hwndRealtimeFilterExclude || !m_hwndRealtimeFilterLogic ||
			!m_hwndRealtimeFilterContext || !m_hwndRealtimeFilterStats ||
			!m_hwndRealtimeFilterAutoScroll || !m_hwndRealtimeFilterCase || !m_hwndRealtimeFilterRegex ||
			!::GetDlgItem(GetHwnd(), IDC_REALTIME_FILTER_COPY) ||
			!::GetDlgItem(GetHwnd(), IDC_REALTIME_FILTER_NEW) ||
			!::GetDlgItem(GetHwnd(), IDC_REALTIME_FILTER_SAVEAS) ||
			!::GetDlgItem(GetHwnd(), IDC_REALTIME_FILTER_CLEAR) ||
			!::GetDlgItem(GetHwnd(), IDC_REALTIME_FILTER_CANCEL) ||
			!::GetDlgItem(GetHwnd(), IDC_REALTIME_FILTER_CLOSE)) {
			destroyFilterControls();
			SendStatusMessage(L"リアルタイムフィルターの画面を作成できませんでした。");
			return;
		}
		::SendMessageW(m_hwndRealtimeFilterLogic, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"AND 条件"));
		::SendMessageW(m_hwndRealtimeFilterLogic, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"OR 条件"));
		::SendMessageW(m_hwndRealtimeFilterLogic, CB_SETCURSEL, 0, 0);
		for (const wchar_t* value : { L"0", L"3", L"5", L"10" }) {
			::SendMessageW(m_hwndRealtimeFilterContext, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
		}
		for (int id = IDC_REALTIME_FILTER_INCLUDE_LABEL; id <= IDC_REALTIME_FILTER_CONTEXT_LABEL; ++id) {
			if (HWND control = ::GetDlgItem(GetHwnd(), id)) {
				::SendMessageW(control, WM_SETFONT,
					reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), FALSE);
			}
		}
		for (const int id : { IDC_REALTIME_FILTER_COPY, IDC_REALTIME_FILTER_NEW,
			IDC_REALTIME_FILTER_SAVEAS, IDC_REALTIME_FILTER_CLEAR, IDC_REALTIME_FILTER_CLOSE,
			IDC_REALTIME_FILTER_AUTOSCROLL, IDC_REALTIME_FILTER_CANCEL }) {
			if (HWND control = ::GetDlgItem(GetHwnd(), id)) {
				::SendMessageW(control, WM_SETFONT,
					reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), FALSE);
			}
		}
		INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES };
		::InitCommonControlsEx(&controls);
		m_hwndRealtimeFilterList = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
			WS_CHILD | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS,
			0, 0, 0, 0, GetHwnd(), nullptr, CEditApp::getInstance()->GetAppInstance(), nullptr);
		if (!m_hwndRealtimeFilterList) {
			destroyFilterControls();
			SendStatusMessage(L"フィルター結果一覧を作成できませんでした。");
			return;
		}
		ListView_SetExtendedListViewStyle(m_hwndRealtimeFilterList,
			LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | LVS_EX_FULLROWSELECT);
		LVCOLUMNW column{};
		column.mask = LVCF_TEXT | LVCF_WIDTH;
		column.pszText = const_cast<LPWSTR>(L"フィルター結果");
		column.cx = DpiScaleX(1200);
		ListView_InsertColumn(m_hwndRealtimeFilterList, 0, &column);
		::SendMessageW(m_hwndRealtimeFilterList, WM_SETFONT,
			reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), FALSE);
	}
	m_bRealtimeFilterMode = true;
	::ShowWindow(m_hwndRealtimeFilterBar, SW_SHOW);
	::ShowWindow(m_hwndRealtimeFilterList, SW_SHOW);
	for (int id = IDC_REALTIME_FILTER_INCLUDE_LABEL; id <= IDC_REALTIME_FILTER_CANCEL; ++id) {
		if (HWND control = ::GetDlgItem(GetHwnd(), id)) ::ShowWindow(control, SW_SHOW);
	}
	::ShowWindow(m_cSplitterWnd.GetHwnd(), SW_HIDE);
	RECT client{};
	::GetClientRect(GetHwnd(), &client);
	OnSize(SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
	::SetFocus(m_hwndRealtimeFilterInclude);
	ScheduleRealtimeFilter();
	m_cToolbar.UpdateToolbar();
}

void CEditWnd::NotifyRealtimeFilterDocumentLoading(CEditDoc* document)
{
	if (!m_bRealtimeFilterMode || document != GetDocument()) return;
	m_bRealtimeFilterDocumentLoading = true;
	::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_DEBOUNCE);
	::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN);
	if (m_pRealtimeFilterService) m_pRealtimeFilterService->Cancel();
	++m_uRealtimeFilterToken;
	m_bRealtimeFilterAppendCachePending = m_bRealtimeFilterScanComplete &&
		document->m_uCurlSyntaxVersion == m_uRealtimeFilterDocumentVersion &&
		m_vRealtimeFilterLineFingerprints.size() == m_vRealtimeFilterLineIndex.size();
	if (m_bRealtimeFilterAppendCachePending) {
		m_vRealtimeFilterAppendCacheFingerprints = std::move(m_vRealtimeFilterLineFingerprints);
		m_vRealtimeFilterAppendCacheMatches = std::move(m_vRealtimeFilterMatches);
	} else {
		m_vRealtimeFilterAppendCacheFingerprints.clear();
		m_vRealtimeFilterAppendCacheMatches.clear();
	}
	m_bRealtimeFilterBatchInFlight = false;
	m_bRealtimeFilterHasPendingBatch = false;
	m_bRealtimeFilterScanComplete = false;
	m_bRealtimeFilterAppendCacheActive = false;
	m_pRealtimeFilterScanLine = nullptr;
	m_vRealtimeFilterPendingBatch.clear();
	m_vRealtimeFilterLineIndex.clear();
	m_vRealtimeFilterLineFingerprints.clear();
	m_vRealtimeFilterMatches.clear();
	m_vRealtimeFilterVisibleRanges.clear();
	if (m_hwndRealtimeFilterList) {
		ListView_SetItemCountEx(m_hwndRealtimeFilterList, 0, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
		::InvalidateRect(m_hwndRealtimeFilterList, nullptr, TRUE);
	}
	if (m_hwndRealtimeFilterStats) ::SetWindowTextW(m_hwndRealtimeFilterStats, L"文書を読み込み中です...");
}

void CEditWnd::NotifyRealtimeFilterDocumentLoadFinished(CEditDoc* document)
{
	if (document != GetDocument()) return;
	m_bRealtimeFilterDocumentLoading = false;
	ScheduleRealtimeFilter(m_bRealtimeFilterAppendCachePending);
}

void CEditWnd::CloseRealtimeFilter(bool refreshLayout)
{
	if (!m_bRealtimeFilterMode) return;
	m_bRealtimeFilterMode = false;
	m_bRealtimeFilterDocumentLoading = false;
	::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_DEBOUNCE);
	::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN);
	if (m_pRealtimeFilterService) m_pRealtimeFilterService->Cancel();
	++m_uRealtimeFilterToken;
	m_bRealtimeFilterBatchInFlight = false;
	m_bRealtimeFilterHasPendingBatch = false;
	m_pRealtimeFilterScanLine = nullptr;
	m_vRealtimeFilterPendingBatch.clear();
	m_vRealtimeFilterLineIndex.clear();
	m_vRealtimeFilterLineFingerprints.clear();
	m_vRealtimeFilterMatches.clear();
	m_vRealtimeFilterVisibleRanges.clear();
	m_vRealtimeFilterAppendCacheFingerprints.clear();
	m_vRealtimeFilterAppendCacheMatches.clear();
	m_bRealtimeFilterAppendCachePending = false;
	m_bRealtimeFilterAppendCacheActive = false;
	if (m_hwndRealtimeFilterBar) ::ShowWindow(m_hwndRealtimeFilterBar, SW_HIDE);
	for (int id = IDC_REALTIME_FILTER_INCLUDE_LABEL; id <= IDC_REALTIME_FILTER_CANCEL; ++id) {
		if (HWND control = ::GetDlgItem(GetHwnd(), id)) ::ShowWindow(control, SW_HIDE);
	}
	if (m_hwndRealtimeFilterList) {
		ListView_SetItemCountEx(m_hwndRealtimeFilterList, 0, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
		::ShowWindow(m_hwndRealtimeFilterList, SW_HIDE);
	}
	if (m_cSplitterWnd.GetHwnd()) ::ShowWindow(m_cSplitterWnd.GetHwnd(), SW_SHOW);
	if (refreshLayout) {
		RECT client{};
		::GetClientRect(GetHwnd(), &client);
		OnSize(SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
		GetActiveView().SetFocus();
	}
	m_cToolbar.UpdateToolbar();
}

void CEditWnd::ScheduleRealtimeFilter(bool keepAppendCache)
{
	if (!m_bRealtimeFilterMode) return;
	if (!keepAppendCache) {
		m_vRealtimeFilterAppendCacheFingerprints.clear();
		m_vRealtimeFilterAppendCacheMatches.clear();
		m_bRealtimeFilterAppendCachePending = false;
		m_bRealtimeFilterAppendCacheActive = false;
	}
	::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_DEBOUNCE);
	::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN);
	if (m_pRealtimeFilterService) m_pRealtimeFilterService->Cancel();
	++m_uRealtimeFilterToken;
	m_bRealtimeFilterBatchInFlight = false;
	m_bRealtimeFilterHasPendingBatch = false;
	m_bRealtimeFilterScanComplete = false;
	m_pRealtimeFilterScanLine = nullptr;
	m_vRealtimeFilterPendingBatch.clear();
	m_vRealtimeFilterLineIndex.clear();
	m_vRealtimeFilterLineFingerprints.clear();
	m_vRealtimeFilterMatches.clear();
	m_vRealtimeFilterVisibleRanges.clear();
	if (m_hwndRealtimeFilterList) {
		ListView_SetItemCountEx(m_hwndRealtimeFilterList, 0, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
		::InvalidateRect(m_hwndRealtimeFilterList, nullptr, TRUE);
	}
	if (m_hwndRealtimeFilterStats) ::SetWindowTextW(m_hwndRealtimeFilterStats, L"条件を解析しています...");
	// A reload can pump window messages while replacing document lines. Keep
	// condition changes, but never scan a partially loaded document.
	if (m_bRealtimeFilterDocumentLoading) {
		if (m_hwndRealtimeFilterStats) ::SetWindowTextW(m_hwndRealtimeFilterStats, L"文書を読み込み中です...");
		return;
	}
	::SetTimer(GetHwnd(), IDT_REALTIME_FILTER_DEBOUNCE, 300, nullptr);
}

void CEditWnd::ContinueRealtimeFilterScan()
{
	if (!m_bRealtimeFilterMode || !GetDocument() || !m_pRealtimeFilterService) return;
	if (GetDocument()->m_uCurlSyntaxVersion != m_uRealtimeFilterDocumentVersion) {
		ScheduleRealtimeFilter();
		return;
	}
	if (m_bRealtimeFilterBatchInFlight) return;
	if (!m_bRealtimeFilterHasPendingBatch) {
		std::vector<textfilter::Line> batch;
		const auto started = std::chrono::steady_clock::now();
		try {
			while (m_pRealtimeFilterScanLine && batch.size() < 2048 &&
				std::chrono::steady_clock::now() - started < std::chrono::milliseconds(4)) {
				const CDocLine* line = m_pRealtimeFilterScanLine;
				const std::size_t lineNumber = m_vRealtimeFilterLineIndex.size();
				const CLogicInt lineLength = line->GetLengthWithoutEOL();
				const std::wstring_view lineText(line->GetPtr(),
					lineLength > CLogicInt(0) ? static_cast<std::size_t>(lineLength) : 0u);
				const std::uint64_t fingerprint = textfilter::FingerprintLine(lineText, line->GetEol().IsValid());
				if (m_bRealtimeFilterAppendCacheActive &&
					lineNumber < m_vRealtimeFilterAppendCacheFingerprints.size() &&
					fingerprint != m_vRealtimeFilterAppendCacheFingerprints[lineNumber]) {
					// The reloaded document was edited or truncated, not merely appended.
					// No batch was sent while validating the old prefix, so restarting at
					// line zero is safe and produces a complete result set.
					m_bRealtimeFilterAppendCacheActive = false;
					m_bRealtimeFilterAppendCachePending = false;
					m_vRealtimeFilterAppendCacheFingerprints.clear();
					m_vRealtimeFilterAppendCacheMatches.clear();
					m_vRealtimeFilterLineIndex.clear();
					m_vRealtimeFilterLineFingerprints.clear();
					m_vRealtimeFilterMatches.clear();
					m_nRealtimeFilterScannedLines = 0;
					m_pRealtimeFilterScanLine = GetDocument()->m_cDocLineMgr.GetDocLineTop();
					::SetTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN, 1, nullptr);
					return;
				}

				m_vRealtimeFilterLineIndex.push_back(line);
				m_vRealtimeFilterLineFingerprints.push_back(fingerprint);
				m_pRealtimeFilterScanLine = line->GetNextLine();

				if (m_bRealtimeFilterAppendCacheActive &&
					lineNumber < m_vRealtimeFilterAppendCacheFingerprints.size()) {
					// Prefix line already validated; reuse its previous match result.
					++m_nRealtimeFilterScannedLines;
					continue;
				}
				if (m_bRealtimeFilterAppendCacheActive) {
					// The entire old document matched by fingerprint. Keep its matches,
					// then send only newly appended lines to the worker.
					m_vRealtimeFilterMatches = std::move(m_vRealtimeFilterAppendCacheMatches);
					m_vRealtimeFilterAppendCacheFingerprints.clear();
					m_vRealtimeFilterAppendCacheMatches.clear();
					m_bRealtimeFilterAppendCacheActive = false;
					m_bRealtimeFilterAppendCachePending = false;
				}
				batch.push_back({ lineNumber, std::wstring(lineText) });
			}
		} catch (...) {
			::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN);
			m_pRealtimeFilterService->Cancel();
			if (m_hwndRealtimeFilterStats) ::SetWindowTextW(m_hwndRealtimeFilterStats, L"メモリ不足でフィルターを中断しました。");
			SendStatusMessage(L"メモリ不足でフィルターを中断しました。");
			return;
		}
		if (m_bRealtimeFilterAppendCacheActive && !m_pRealtimeFilterScanLine) {
			if (m_vRealtimeFilterLineIndex.size() < m_vRealtimeFilterAppendCacheFingerprints.size()) {
				// A shorter reload is not an append. Restart with a full scan.
				m_bRealtimeFilterAppendCacheActive = false;
				m_bRealtimeFilterAppendCachePending = false;
				m_vRealtimeFilterAppendCacheFingerprints.clear();
				m_vRealtimeFilterAppendCacheMatches.clear();
				m_vRealtimeFilterLineIndex.clear();
				m_vRealtimeFilterLineFingerprints.clear();
				m_vRealtimeFilterMatches.clear();
				m_nRealtimeFilterScannedLines = 0;
				m_pRealtimeFilterScanLine = GetDocument()->m_cDocLineMgr.GetDocLineTop();
				::SetTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN, 1, nullptr);
				return;
			}
			// Exact same content after reload: reuse every match and publish an
			// empty final batch so the UI can finish normally.
			m_vRealtimeFilterMatches = std::move(m_vRealtimeFilterAppendCacheMatches);
			m_vRealtimeFilterAppendCacheFingerprints.clear();
			m_vRealtimeFilterAppendCacheMatches.clear();
			m_bRealtimeFilterAppendCacheActive = false;
			m_bRealtimeFilterAppendCachePending = false;
		}
		if (batch.empty() && m_pRealtimeFilterScanLine && m_bRealtimeFilterAppendCacheActive) {
			const std::wstring progress = L"一致 " + std::to_wstring(m_vRealtimeFilterAppendCacheMatches.size()) +
				L" 行 / 追記確認 " + std::to_wstring(m_nRealtimeFilterScannedLines) + L" / " +
				std::to_wstring(m_nRealtimeFilterTotalLines) + L" 行";
			if (m_hwndRealtimeFilterStats) ::SetWindowTextW(m_hwndRealtimeFilterStats, progress.c_str());
			::SetTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN, 1, nullptr);
			return;
		}
		m_vRealtimeFilterPendingBatch = std::move(batch);
		m_bRealtimeFilterPendingBatchFinal = m_pRealtimeFilterScanLine == nullptr;
		m_bRealtimeFilterHasPendingBatch = true;
	}
	if (m_pRealtimeFilterService->Submit(m_uRealtimeFilterToken,
		std::move(m_vRealtimeFilterPendingBatch), m_bRealtimeFilterPendingBatchFinal)) {
		m_vRealtimeFilterPendingBatch.clear();
		m_bRealtimeFilterHasPendingBatch = false;
		m_bRealtimeFilterBatchInFlight = true;
		if (m_bRealtimeFilterPendingBatchFinal) ::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN);
	} else {
		::SetTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN, 10, nullptr);
	}
}

void CEditWnd::AcceptRealtimeFilterResult()
{
	std::optional<textfilter::BatchResult> result;
	{
		std::lock_guard<std::mutex> lock(m_realtimeFilterMailboxMutex);
		result.swap(m_realtimeFilterMailbox);
	}
	if (!result || !m_bRealtimeFilterMode || !GetDocument() ||
		result->requestToken != m_uRealtimeFilterToken ||
		result->documentVersion != GetDocument()->m_uCurlSyntaxVersion) return;
	m_bRealtimeFilterBatchInFlight = false;
	if (result->failed) {
		m_bRealtimeFilterScanComplete = false;
		::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN);
		if (m_hwndRealtimeFilterStats) ::SetWindowTextW(m_hwndRealtimeFilterStats, result->error.c_str());
		SendStatusMessage(result->error.c_str());
		return;
	}
	m_nRealtimeFilterScannedLines += result->scannedLines;
	try {
		m_vRealtimeFilterMatches.insert(m_vRealtimeFilterMatches.end(),
			result->matchingLines.begin(), result->matchingLines.end());
	} catch (...) {
		m_bRealtimeFilterScanComplete = false;
		::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN);
		if (m_pRealtimeFilterService) m_pRealtimeFilterService->Cancel();
		if (m_hwndRealtimeFilterStats) ::SetWindowTextW(m_hwndRealtimeFilterStats, L"結果を保存できるメモリがありません。");
		SendStatusMessage(L"結果を保存できるメモリがありません。");
		return;
	}
	m_bRealtimeFilterScanComplete = result->finalBatch;
	try {
		const std::size_t contextRows = GetRealtimeFilterContextRows(m_hwndRealtimeFilterContext);
		m_vRealtimeFilterVisibleRanges = textfilter::BuildVisibleRanges(m_vRealtimeFilterMatches,
			m_vRealtimeFilterLineIndex.size(), contextRows);
	} catch (...) {
		m_vRealtimeFilterVisibleRanges.clear();
		m_bRealtimeFilterScanComplete = false;
		::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN);
		if (m_pRealtimeFilterService) m_pRealtimeFilterService->Cancel();
		ListView_SetItemCountEx(m_hwndRealtimeFilterList, 0, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
		::InvalidateRect(m_hwndRealtimeFilterList, nullptr, TRUE);
		if (m_hwndRealtimeFilterStats) ::SetWindowTextW(m_hwndRealtimeFilterStats, L"表示範囲を作成できるメモリがありません。");
		SendStatusMessage(L"表示範囲を作成できるメモリがありません。");
		return;
	}
	const std::size_t visible = textfilter::CountVisibleLines(m_vRealtimeFilterVisibleRanges);
	ListView_SetItemCountEx(m_hwndRealtimeFilterList,
		static_cast<int>(std::min<std::size_t>(visible, static_cast<std::size_t>(std::numeric_limits<int>::max()))),
		LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
	::InvalidateRect(m_hwndRealtimeFilterList, nullptr, TRUE);
	if (m_bRealtimeFilterAutoScroll && !m_vRealtimeFilterMatches.empty() && visible > 0) {
		const std::size_t targetLine = m_vRealtimeFilterMatches.back();
		std::size_t displayRow = 0;
		for (const auto& range : m_vRealtimeFilterVisibleRanges) {
			if (targetLine >= range.first && targetLine <= range.last) {
				displayRow += targetLine - range.first;
				ListView_EnsureVisible(m_hwndRealtimeFilterList,
					static_cast<int>(std::min<std::size_t>(displayRow, static_cast<std::size_t>(std::numeric_limits<int>::max()))), FALSE);
				break;
			}
			displayRow += range.last - range.first + 1;
		}
	}
	if (result->finalBatch) {
		const std::wstring stats = L"一致 " + std::to_wstring(m_vRealtimeFilterMatches.size()) +
			L" 行 / 表示 " + std::to_wstring(visible) + L" 行 / 全 " +
			std::to_wstring(m_vRealtimeFilterLineIndex.size()) + L" 行";
		if (m_hwndRealtimeFilterStats) ::SetWindowTextW(m_hwndRealtimeFilterStats, stats.c_str());
	} else {
		const std::wstring progress = L"一致 " + std::to_wstring(m_vRealtimeFilterMatches.size()) +
			L" 行 / 表示 " + std::to_wstring(visible) + L" 行 / 読込 " +
			std::to_wstring(m_nRealtimeFilterScannedLines) + L" / " +
			std::to_wstring(m_nRealtimeFilterTotalLines) + L" 行";
		if (m_hwndRealtimeFilterStats) ::SetWindowTextW(m_hwndRealtimeFilterStats, progress.c_str());
		::SetTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN, 1, nullptr);
	}
}

void CEditWnd::UpdateRealtimeFilterLayout()
{
	if (!m_bRealtimeFilterMode || !m_hwndRealtimeFilterList || !m_cSplitterWnd.GetHwnd()) return;
	RECT rect{};
	if (!::GetWindowRect(m_cSplitterWnd.GetHwnd(), &rect)) return;
	::MapWindowPoints(nullptr, GetHwnd(), reinterpret_cast<POINT*>(&rect), 2);
	::SetWindowPos(m_hwndRealtimeFilterList, HWND_TOP, rect.left, rect.top,
		std::max<LONG>(0, rect.right - rect.left), std::max<LONG>(0, rect.bottom - rect.top),
		SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CEditWnd::ExportRealtimeFilter(bool toNewDocument, bool showSaveAs)
{
	if (!m_bRealtimeFilterMode || !m_bRealtimeFilterScanComplete || !GetDocument()) {
		SendStatusMessage(L"フィルター処理が完了してから出力してください。");
		return;
	}
	std::wstring text;
	try {
		for (const auto& range : m_vRealtimeFilterVisibleRanges) {
			for (std::size_t lineNumber = range.first; lineNumber <= range.last; ++lineNumber) {
				if (lineNumber >= m_vRealtimeFilterLineIndex.size()) break;
				const CDocLine* line = m_vRealtimeFilterLineIndex[lineNumber];
				if (!line) continue;
				const CLogicInt lineLength = line->GetLengthWithoutEOL();
				if (lineLength > CLogicInt(0)) text.append(line->GetPtr(), static_cast<std::size_t>(lineLength));
				if (lineNumber < m_vRealtimeFilterLineIndex.size() - 1 || line->GetEol().IsValid()) {
					const auto& ending = line->GetEol().GetData().GetVal();
					if (!ending.empty()) text.append(ending);
				}
			}
		}
	} catch (...) {
		SendStatusMessage(L"フィルター結果の出力用メモリを確保できませんでした。");
		return;
	}
	if (text.size() >= std::numeric_limits<SIZE_T>::max() / sizeof(wchar_t)) {
		SendStatusMessage(L"フィルター結果が大きすぎてクリップボードへ出力できません。");
		return;
	}
	const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
	HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (!memory) {
		SendStatusMessage(L"クリップボード用のメモリを確保できませんでした。");
		return;
	}
	void* destination = ::GlobalLock(memory);
	if (!destination) {
		::GlobalFree(memory);
		SendStatusMessage(L"クリップボード用メモリをロックできませんでした。");
		return;
	}
	memcpy(destination, text.c_str(), bytes);
	::GlobalUnlock(memory);
	if (!::OpenClipboard(GetHwnd())) {
		::GlobalFree(memory);
		SendStatusMessage(L"クリップボードを開けませんでした。");
		return;
	}
	::EmptyClipboard();
	if (!::SetClipboardData(CF_UNICODETEXT, memory)) {
		::CloseClipboard();
		::GlobalFree(memory);
		SendStatusMessage(L"フィルター結果をクリップボードへ設定できませんでした。");
		return;
	}
	::CloseClipboard();
	if (!toNewDocument) {
		SendStatusMessage(L"表示中の行をクリップボードへコピーしました。");
		return;
	}
	std::vector<HWND> before;
	EditNode* nodes = nullptr;
	const int count = CAppNodeManager::getInstance()->GetOpenedWindowArr(&nodes, FALSE);
	for (int i = 0; i < count; ++i) before.push_back(nodes[i].GetHwnd());
	delete[] nodes;
	// OpenNewEditor's synchronous mode waits for the new editor's initialization
	// event while pumping sent messages. This avoids racing the process startup
	// when the empty document is created in another Sakura process.
	SLoadInfo loadInfo(L"", CODE_NONE, false);
	if (!CControlTray::OpenNewEditor(G_AppInstance(), GetHwnd(), loadInfo, nullptr, true, nullptr, false)) {
		SendStatusMessage(L"新規文書を作成できませんでした。表示行はクリップボードにあります。");
		return;
	}
	nodes = nullptr;
	const int afterCount = CAppNodeManager::getInstance()->GetOpenedWindowArr(&nodes, FALSE);
	HWND newWindow = nullptr;
	for (int i = 0; i < afterCount && !newWindow; ++i) {
		const HWND candidate = nodes[i].GetHwnd();
		if (std::find(before.begin(), before.end(), candidate) == before.end()) newWindow = candidate;
	}
	delete[] nodes;
	// Never paste filtered text into an unrelated foreground document.
	if (!newWindow || newWindow == GetHwnd()) {
		SendStatusMessage(L"新規文書を特定できませんでした。表示行はクリップボードにあります。");
		return;
	}
	::SendMessageCmd(newWindow, WM_COMMAND, MAKEWPARAM(F_PASTE, 0), 0);
	if (showSaveAs) {
		::SendMessageCmd(newWindow, WM_COMMAND, MAKEWPARAM(F_FILESAVEAS_DIALOG, 0), 0);
		SendStatusMessage(L"フィルター結果を新規文書に出力し、名前を付けて保存を開きました。");
	} else {
		SendStatusMessage(L"フィルター結果を新規文書へ出力しました。Ctrl+S で保存できます。");
	}
}

void CEditWnd::StartRealtimeFilterScan()
{
	if (!m_bRealtimeFilterMode || m_bRealtimeFilterDocumentLoading || !GetDocument()) return;
	try {
		if (!m_pRealtimeFilterService) m_pRealtimeFilterService = std::make_unique<textfilter::Service>();
		textfilter::Options options;
		auto readTerms = [](HWND edit, std::vector<std::wstring>& terms) {
			const int length = ::GetWindowTextLengthW(edit);
			std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1, L'\0');
			::GetWindowTextW(edit, value.data(), length + 1);
			value.resize(static_cast<std::size_t>(std::max(0, length)));
			std::size_t begin = 0;
			while (begin <= value.size()) {
				const std::size_t end = value.find(L'\n', begin);
				std::wstring term = value.substr(begin, end == std::wstring::npos ? end : end - begin);
				while (!term.empty() && std::iswspace(term.front())) term.erase(term.begin());
				while (!term.empty() && std::iswspace(term.back())) term.pop_back();
				if (!term.empty()) terms.push_back(std::move(term));
				if (end == std::wstring::npos) break;
				begin = end + 1;
			}
		};
		readTerms(m_hwndRealtimeFilterInclude, options.includeTerms);
		readTerms(m_hwndRealtimeFilterExclude, options.excludeTerms);
		options.logic = ::SendMessageW(m_hwndRealtimeFilterLogic, CB_GETCURSEL, 0, 0) == 1
			? textfilter::Logic::Or : textfilter::Logic::And;
		options.caseSensitive = ::SendMessageW(m_hwndRealtimeFilterCase, BM_GETCHECK, 0, 0) == BST_CHECKED;
		options.regularExpression = ::SendMessageW(m_hwndRealtimeFilterRegex, BM_GETCHECK, 0, 0) == BST_CHECKED;
		options.regexDllPath = GetDllShareData().m_Common.m_sSearch.m_szRegexpLib;
		options.contextRows = GetRealtimeFilterContextRows(m_hwndRealtimeFilterContext);
		m_uRealtimeFilterDocumentVersion = GetDocument()->m_uCurlSyntaxVersion;
		m_nRealtimeFilterTotalLines = static_cast<std::size_t>(std::max<CLogicInt>(0, GetDocument()->m_cDocLineMgr.GetLineCount()));
		m_nRealtimeFilterScannedLines = 0;
		m_pRealtimeFilterScanLine = GetDocument()->m_cDocLineMgr.GetDocLineTop();
		m_vRealtimeFilterLineIndex.clear();
		m_vRealtimeFilterLineFingerprints.clear();
		m_vRealtimeFilterMatches.clear();
		m_vRealtimeFilterVisibleRanges.clear();
		m_bRealtimeFilterAppendCacheActive = m_bRealtimeFilterAppendCachePending;
		m_bRealtimeFilterBatchInFlight = false;
		m_bRealtimeFilterHasPendingBatch = false;
		m_bRealtimeFilterScanComplete = false;
		const std::uint64_t token = m_uRealtimeFilterToken;
		const HWND resultWindow = GetHwnd();
		m_pRealtimeFilterService->Begin(token, m_uRealtimeFilterDocumentVersion, std::move(options),
			[this, resultWindow](textfilter::BatchResult result) {
				{
					std::lock_guard<std::mutex> lock(m_realtimeFilterMailboxMutex);
					m_realtimeFilterMailbox = std::move(result);
				}
				if (!::PostMessage(resultWindow, MYWM_REALTIME_FILTER_RESULT, 0, 0)) {
					std::lock_guard<std::mutex> lock(m_realtimeFilterMailboxMutex);
					m_realtimeFilterMailbox.reset();
				}
			});
		::SetTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN, 1, nullptr);
	} catch (...) {
		if (m_hwndRealtimeFilterStats) ::SetWindowTextW(m_hwndRealtimeFilterStats, L"フィルターを開始できませんでした。");
		SendStatusMessage(L"フィルターを開始できませんでした。");
	}
}

void CEditWnd::ToggleCSVTableMode()
{
	if (m_bCSVTableMode) {
		FinishCSVCellEdit(true);
		m_nCSVPendingEditRow = m_nCSVPendingEditColumn = -1;
		m_nCSVSortPendingSourceRow = -1;
		m_uCSVValidationPendingOffset = static_cast<std::size_t>(-1);
		::KillTimer(GetHwnd(), IDT_CSV_SELECTION_STATS);
		m_bCSVTableMode = false;
		++m_uCSVQueryGeneration;
		if (m_pCSVTableParseService) m_pCSVTableParseService->Cancel();
		if (m_pCSVTableQueryService) m_pCSVTableQueryService->Cancel();
		if (m_pCSVValidationService) m_pCSVValidationService->Cancel();
		m_bCSVValidationListActive = false;
		m_bCSVValidationStale = false;
		m_bCSVValidationHasResult = false;
		m_vCSVValidationDiagnostics.clear();
		RefreshCurlSyntaxErrorList();
		if (m_hwndCSVTable) ::ShowWindow(m_hwndCSVTable, SW_HIDE);
		if (m_cSplitterWnd.GetHwnd()) ::ShowWindow(m_cSplitterWnd.GetHwnd(), SW_SHOW);
		m_csvTableModel.reset();
		m_csvTableSnapshot.reset();
		m_vCSVTableViewRows.clear();
		m_bCSVTableViewOrderIdentity = true;
		GetActiveView().SetFocus();
		m_cToolbar.UpdateToolbar();
		return;
	}
	if (!GetDocument() || !m_cSplitterWnd.GetHwnd()) return;
	if (!m_hwndCSVTable) {
		INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES };
		::InitCommonControlsEx(&controls);
		m_hwndCSVTable = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
			WS_CHILD | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS,
			0, 0, 0, 0, GetHwnd(), nullptr, CEditApp::getInstance()->GetAppInstance(), nullptr);
		if (!m_hwndCSVTable) {
			SendStatusMessage(L"CSVテーブルを作成できませんでした。");
			return;
		}
		ListView_SetExtendedListViewStyle(m_hwndCSVTable,
			LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
		::SendMessage(m_hwndCSVTable, WM_SETFONT,
			reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), FALSE);
	}
	m_bCSVTableMode = true;
	::ShowWindow(m_cSplitterWnd.GetHwnd(), SW_HIDE);
	::ShowWindow(m_hwndCSVTable, SW_SHOW);
	UpdateCSVTableLayout();
	::SetFocus(m_hwndCSVTable);
	ScheduleCSVTableParse();
	m_cToolbar.UpdateToolbar();
}

void CEditWnd::RunCSVCheck()
{
	if (!m_bCSVTableMode) {
		SendStatusMessage(L"CSVテーブル表示に切り替えてから CSV チェックを実行してください。");
		return;
	}
	if (m_bCSVValidationListActive && m_bCurlSyntaxErrorListVisible && !m_bCSVValidationStale) {
		m_bCSVValidationListActive = false;
		m_bCurlSyntaxErrorListVisible = false;
		if (m_hwndCurlSyntaxErrorSummary) ::ShowWindow(m_hwndCurlSyntaxErrorSummary, SW_HIDE);
		if (m_hwndCurlSyntaxErrorList) ::ShowWindow(m_hwndCurlSyntaxErrorList, SW_HIDE);
		RECT client{};
		::GetClientRect(GetHwnd(), &client);
		OnSize(SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
		return;
	}
	const CEditDoc* currentDocument = GetDocument();
	if (!m_bCSVValidationListActive && m_bCSVValidationHasResult && !m_bCSVValidationStale &&
		currentDocument && m_uCSVValidationVersion == currentDocument->m_uCurlSyntaxVersion) {
		m_bCSVValidationListActive = true;
		CreateCurlSyntaxErrorList();
		if (!m_hwndCurlSyntaxErrorList) return;
		m_bCurlSyntaxErrorListVisible = true;
		if (m_hwndCurlSyntaxErrorSummary) ::ShowWindow(m_hwndCurlSyntaxErrorSummary, SW_SHOW);
		::ShowWindow(m_hwndCurlSyntaxErrorList, SW_SHOW);
		RefreshCurlSyntaxErrorList();
		RECT client{};
		::GetClientRect(GetHwnd(), &client);
		OnSize(SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
		return;
	}
	if (!m_csvTableSnapshot || !GetDocument() ||
		m_csvTableSnapshot->DocumentVersion() != GetDocument()->m_uCurlSyntaxVersion) {
		SendStatusMessage(L"CSV テーブルの解析が完了してから実行してください。");
		return;
	}
	try {
		if (!m_pCSVValidationService) m_pCSVValidationService = std::make_unique<csvtable::CSVValidationService>();
	} catch (...) {
		SendStatusMessage(L"CSV チェックを開始できませんでした。");
		return;
	}
	m_bCSVValidationListActive = true;
	m_bCSVValidationStale = true;
	CreateCurlSyntaxErrorList();
	if (!m_hwndCurlSyntaxErrorList) return;
	m_bCurlSyntaxErrorListVisible = true;
	if (m_hwndCurlSyntaxErrorSummary) ::ShowWindow(m_hwndCurlSyntaxErrorSummary, SW_SHOW);
	::ShowWindow(m_hwndCurlSyntaxErrorList, SW_SHOW);
	RefreshCurlSyntaxErrorList();
	RECT client{};
	::GetClientRect(GetHwnd(), &client);
	OnSize(SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
	CEditDoc* document = GetDocument();
	const std::uint64_t documentId = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(document));
	const HWND resultWindow = GetHwnd();
	try {
		csvtable::ValidationOptions options;
		options.hasHeader = m_bCSVHasHeader;
		m_pCSVValidationService->Schedule(documentId, m_csvTableSnapshot, std::move(options),
			[this, resultWindow](csvtable::ValidationResult result) {
				{
					std::lock_guard<std::mutex> lock(m_csvValidationMailboxMutex);
					m_csvValidationMailbox = std::move(result);
				}
				if (!::PostMessage(resultWindow, MYWM_CSV_VALIDATION_RESULT, 0, 0)) {
					std::lock_guard<std::mutex> lock(m_csvValidationMailboxMutex);
					m_csvValidationMailbox.reset();
				}
			});
	} catch (...) {
		m_bCSVValidationStale = false;
		SendStatusMessage(L"CSV チェック要求を作成できませんでした。");
	}
}

void CEditWnd::AcceptCSVValidationResult()
{
	std::optional<csvtable::ValidationResult> result;
	{
		std::lock_guard<std::mutex> lock(m_csvValidationMailboxMutex);
		result.swap(m_csvValidationMailbox);
	}
	if (!m_bCSVTableMode || !m_bCSVValidationListActive || !result) return;
	const CEditDoc* document = GetDocument();
	const std::uint64_t documentId = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(document));
	if (!document || result->documentId != documentId ||
		result->documentVersion != document->m_uCurlSyntaxVersion || !m_csvTableSnapshot ||
		result->documentVersion != m_csvTableSnapshot->DocumentVersion()) return;
	if (result->failed) {
		m_bCSVValidationStale = false;
		m_bCSVValidationHasResult = false;
		m_vCSVValidationDiagnostics.clear();
		SendStatusMessage(L"CSV チェックに失敗しました。");
	} else {
		m_bCSVValidationStale = false;
		m_bCSVValidationHasResult = true;
		m_uCSVValidationVersion = result->documentVersion;
		m_vCSVValidationDiagnostics = std::move(result->diagnostics);
	}
	RefreshCurlSyntaxErrorList();
}

void CEditWnd::SetCSVDelimiter(wchar_t delimiter, bool autoDetect)
{
	if (delimiter == L'"' || delimiter == L'\r' || delimiter == L'\n' || delimiter == L'\0') return;
	m_chCSVDelimiter = delimiter;
	m_bCSVAutoDetectDelimiter = autoDetect;
	auto& settings = csvtable::GetUserSettings();
	settings.delimiter = delimiter;
	settings.autoDetectDelimiter = autoDetect;
	if (m_bCSVValidationListActive || m_bCSVValidationHasResult) {
		m_bCSVValidationStale = true;
		if (m_pCSVValidationService) m_pCSVValidationService->Cancel();
		RefreshCurlSyntaxErrorList();
	}
	if (m_bCSVTableMode) ScheduleCSVTableParse();
}

void CEditWnd::ToggleCSVHeader()
{
	m_bCSVHasHeader = !m_bCSVHasHeader;
	csvtable::GetUserSettings().hasHeader = m_bCSVHasHeader;
	if (!m_bCSVTableMode || !m_csvTableSnapshot) return;
	if (m_bCSVValidationListActive || m_bCSVValidationHasResult) {
		m_bCSVValidationStale = true;
		if (m_pCSVValidationService) m_pCSVValidationService->Cancel();
		RefreshCurlSyntaxErrorList();
	}
	try {
		m_csvTableModel = std::make_unique<csvtable::CSVTableModel>(m_csvTableSnapshot, m_bCSVHasHeader);
	} catch (...) {
		m_bCSVHasHeader = !m_bCSVHasHeader;
		csvtable::GetUserSettings().hasHeader = m_bCSVHasHeader;
		SendStatusMessage(L"CSV のヘッダー設定を切り替えられませんでした。");
		return;
	}
	m_nCSVPendingEditRow = m_nCSVPendingEditColumn = -1;
	PopulateCSVTableColumns();
	ScheduleCSVTableQuery();
}

void CEditWnd::ScheduleCSVTableParse()
{
	if (!m_bCSVTableMode || !GetDocument()) return;
	++m_uCSVQueryGeneration;
	if (m_pCSVTableQueryService) m_pCSVTableQueryService->Cancel();
	m_csvTableModel.reset();
	m_csvTableSnapshot.reset();
	m_vCSVTableViewRows.clear();
	m_bCSVTableViewOrderIdentity = true;
	if (m_hwndCSVTable) ListView_SetItemCountEx(m_hwndCSVTable, 0, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
	const auto clearTableProjection = [this]() {
		if (m_pCSVTableParseService) m_pCSVTableParseService->Cancel();
		m_csvTableModel.reset();
		m_csvTableSnapshot.reset();
		if (m_hwndCSVTable) {
			ListView_SetItemCountEx(m_hwndCSVTable, 0, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
			::InvalidateRect(m_hwndCSVTable, nullptr, TRUE);
		}
	};
	const CEditDoc* document = GetDocument();
	std::size_t length = 0;
	for (const CDocLine* line = document->m_cDocLineMgr.GetDocLineTop(); line; line = line->GetNextLine()) {
		const int lineLength = static_cast<int>(line->GetLengthWithEOL());
		if (lineLength < 0 || static_cast<std::size_t>(lineLength) > static_cast<std::size_t>(std::numeric_limits<int>::max()) - length) {
			clearTableProjection();
			SendStatusMessage(L"文書が大きすぎるため CSV テーブル表示にできません。");
			return;
		}
		length += static_cast<std::size_t>(lineLength);
	}
	std::wstring text;
	try {
		text.reserve(length);
		for (const CDocLine* line = document->m_cDocLineMgr.GetDocLineTop(); line; line = line->GetNextLine()) {
			const int lineLength = static_cast<int>(line->GetLengthWithEOL());
			if (lineLength > 0) text.append(line->GetPtr(), static_cast<std::size_t>(lineLength));
		}
	} catch (...) {
		clearTableProjection();
		SendStatusMessage(L"CSV テーブル用のメモリを確保できませんでした。");
		return;
	}
	try {
		if (!m_pCSVTableParseService) m_pCSVTableParseService = std::make_unique<csvtable::CSVTableParseService>();
	} catch (...) {
		clearTableProjection();
		SendStatusMessage(L"CSV テーブル解析を開始できませんでした。");
		return;
	}
	csvtable::ParseOptions options;
	options.delimiter = m_chCSVDelimiter;
	options.autoDetectDelimiter = m_bCSVAutoDetectDelimiter;
	const std::uint64_t documentId = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(document));
	const std::uint64_t version = document->m_uCurlSyntaxVersion;
	const HWND resultWindow = GetHwnd();
	m_pCSVTableParseService->Schedule(documentId, version, std::move(text), options,
		[this, resultWindow](csvtable::TableParseResult result) {
			{
				std::lock_guard<std::mutex> lock(m_csvTableMailboxMutex);
				m_csvTableMailbox = std::move(result);
			}
			if (!::PostMessage(resultWindow, MYWM_CSV_TABLE_PARSE_RESULT, 0, 0)) {
				std::lock_guard<std::mutex> lock(m_csvTableMailboxMutex);
				m_csvTableMailbox.reset();
			}
		}, true);
}

void CEditWnd::AcceptCSVTableParseResult()
{
	std::optional<csvtable::TableParseResult> result;
	{
		std::lock_guard<std::mutex> lock(m_csvTableMailboxMutex);
		result.swap(m_csvTableMailbox);
	}
	if (!m_bCSVTableMode || !result) return;
	const CEditDoc* document = GetDocument();
	const std::uint64_t documentId = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(document));
	if (!document || result->documentId != documentId || result->documentVersion != document->m_uCurlSyntaxVersion) {
		ScheduleCSVTableParse();
		return;
	}
	if (result->failed || !result->snapshot) {
		m_csvTableModel.reset();
		m_csvTableSnapshot.reset();
		if (m_hwndCSVTable) ListView_SetItemCountEx(m_hwndCSVTable, 0, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
		SendStatusMessage(L"CSV テーブルの解析に失敗しました。テキスト表示へ戻してください。");
		return;
	}
	try {
		m_csvTableSnapshot = std::move(result->snapshot);
		m_csvTableModel = std::make_unique<csvtable::CSVTableModel>(m_csvTableSnapshot, m_bCSVHasHeader);
		const auto previousFilterCount = m_vCSVFilters.size();
		m_vCSVFilters.erase(std::remove_if(m_vCSVFilters.begin(), m_vCSVFilters.end(),
			[this](const csvtable::Filter& filter) { return filter.column >= m_csvTableModel->ColumnCount(); }),
			m_vCSVFilters.end());
		if (previousFilterCount != m_vCSVFilters.size()) {
			SendStatusMessage(L"列構成が変わったため、一部の CSV フィルターを解除しました。");
		}
		m_vCSVSortKeys.erase(std::remove_if(m_vCSVSortKeys.begin(), m_vCSVSortKeys.end(),
			[this](const csvtable::SortKey& key) { return key.column >= m_csvTableModel->ColumnCount(); }),
			m_vCSVSortKeys.end());
		PopulateCSVTableColumns();
	} catch (...) {
		m_csvTableModel.reset();
		m_csvTableSnapshot.reset();
		if (m_hwndCSVTable) ListView_SetItemCountEx(m_hwndCSVTable, 0, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
		SendStatusMessage(L"CSV テーブルの表示用メモリを確保できませんでした。テキスト表示へ戻してください。");
		return;
	}
	const auto errorCount = m_csvTableSnapshot->Parsed().errors.size();
	if (errorCount != 0) {
		const std::wstring message = L"CSV 構文エラー " + std::to_wstring(errorCount) + L" 件。原文は保持されています。";
		SendStatusMessage(message.c_str());
	}
	if (!m_vCSVSortKeys.empty() || !m_vCSVFilters.empty()) {
		ScheduleCSVTableQuery();
	} else if (m_nCSVPendingEditRow >= 0 && m_nCSVPendingEditColumn > 0) {
		const int row = m_nCSVPendingEditRow;
		const int column = m_nCSVPendingEditColumn;
		m_nCSVPendingEditRow = m_nCSVPendingEditColumn = -1;
		BeginCSVCellEdit(row, column);
	}
}

void CEditWnd::ScheduleCSVTableQuery()
{
	if (!m_bCSVTableMode || !m_csvTableModel || !m_csvTableSnapshot || !GetDocument()) return;
	const std::uint64_t requestToken = ++m_uCSVQueryGeneration;
	if (m_vCSVSortKeys.empty() && m_vCSVFilters.empty()) {
		if (m_pCSVTableQueryService) m_pCSVTableQueryService->Cancel();
		m_vCSVTableViewRows.clear();
		m_bCSVTableViewOrderIdentity = true;
		ListView_SetItemCountEx(m_hwndCSVTable, static_cast<int>(m_csvTableModel->RowCount()),
			LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
		UpdateCSVSortIndicator();
		::InvalidateRect(m_hwndCSVTable, nullptr, TRUE);
		SendStatusMessage((L"表示件数 " + std::to_wstring(m_csvTableModel->RowCount()) +
			L" 件 / 全件数 " + std::to_wstring(m_csvTableModel->RowCount()) + L" 件").c_str());
		if (m_nCSVSortPendingSourceRow >= 0) {
			const int row = m_nCSVSortPendingSourceRow;
			m_nCSVSortPendingSourceRow = -1;
			ListView_SetItemState(m_hwndCSVTable, -1, 0, LVIS_FOCUSED | LVIS_SELECTED);
			if (static_cast<std::size_t>(row) < m_csvTableModel->RowCount()) {
				ListView_SetItemState(m_hwndCSVTable, row, LVIS_FOCUSED | LVIS_SELECTED,
					LVIS_FOCUSED | LVIS_SELECTED);
				ListView_EnsureVisible(m_hwndCSVTable, row, FALSE);
			}
		}
		if (m_nCSVPendingEditRow >= 0 && m_nCSVPendingEditColumn > 0) {
			const int row = m_nCSVPendingEditRow;
			const int column = m_nCSVPendingEditColumn;
			m_nCSVPendingEditRow = m_nCSVPendingEditColumn = -1;
			BeginCSVCellEdit(row, column);
		}
		return;
	}
	try {
		if (!m_pCSVTableQueryService) m_pCSVTableQueryService = std::make_unique<csvtable::CSVTableQueryService>();
	} catch (...) {
		SendStatusMessage(L"CSV の並べ替え／絞り込みを開始できませんでした。");
		return;
	}
	const std::vector<csvtable::SortKey> sortKeys = m_vCSVSortKeys;
	const CEditDoc* document = GetDocument();
	const std::uint64_t documentId = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(document));
	const std::uint64_t version = m_csvTableModel->DocumentVersion();
	const HWND resultWindow = GetHwnd();
	try {
		m_pCSVTableQueryService->Schedule(documentId, version, m_csvTableSnapshot, m_bCSVHasHeader,
			m_vCSVFilters, m_csvFilterLogic, std::move(sortKeys),
			[this, resultWindow, requestToken](csvtable::TableQueryResult result) {
				result.requestToken = requestToken;
				{
					std::lock_guard<std::mutex> lock(m_csvTableQueryMailboxMutex);
					m_csvTableQueryMailbox = std::move(result);
				}
				if (!::PostMessage(resultWindow, MYWM_CSV_TABLE_QUERY_RESULT, 0, 0)) {
					std::lock_guard<std::mutex> lock(m_csvTableQueryMailboxMutex);
					m_csvTableQueryMailbox.reset();
				}
			});
	} catch (...) {
		SendStatusMessage(L"CSV の並べ替え／絞り込み要求を作成できませんでした。");
	}
}

void CEditWnd::AcceptCSVTableQueryResult()
{
	std::optional<csvtable::TableQueryResult> result;
	{
		std::lock_guard<std::mutex> lock(m_csvTableQueryMailboxMutex);
		result.swap(m_csvTableQueryMailbox);
	}
	if (!m_bCSVTableMode || !result) return;
	const CEditDoc* document = GetDocument();
	const std::uint64_t documentId = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(document));
	if (!document || result->requestToken != m_uCSVQueryGeneration ||
		result->documentId != documentId || !m_csvTableModel ||
		result->documentVersion != document->m_uCurlSyntaxVersion ||
		result->documentVersion != m_csvTableModel->DocumentVersion()) return;
	if (result->failed) {
		m_nCSVSortPendingSourceRow = -1;
		m_nCSVPendingEditRow = m_nCSVPendingEditColumn = -1;
		m_uCSVValidationPendingOffset = static_cast<std::size_t>(-1);
		SendStatusMessage(L"CSV の並べ替えに失敗しました。表示順は変更していません。");
		return;
	}
	if (result->viewOrder.invalidFilter != static_cast<std::size_t>(-1)) {
		m_nCSVSortPendingSourceRow = -1;
		m_nCSVPendingEditRow = m_nCSVPendingEditColumn = -1;
		m_uCSVValidationPendingOffset = static_cast<std::size_t>(-1);
		SendStatusMessage(L"CSV の正規表現または数値フィルター条件を確認してください。");
		return;
	}
	m_bCSVTableViewOrderIdentity = result->viewOrder.identity;
	m_vCSVTableViewRows = std::move(result->viewOrder.rows);
	bool invalidRows = !m_bCSVTableViewOrderIdentity &&
		m_vCSVTableViewRows.size() > m_csvTableModel->RowCount();
	if (!m_bCSVTableViewOrderIdentity && !invalidRows) {
		invalidRows = std::any_of(m_vCSVTableViewRows.begin(), m_vCSVTableViewRows.end(),
			[this](std::size_t row) { return row >= m_csvTableModel->RowCount(); });
	}
	if (invalidRows) {
		m_vCSVTableViewRows.clear();
		m_bCSVTableViewOrderIdentity = true;
		SendStatusMessage(L"CSV の表示順を適用できませんでした。元の順序を表示します。");
	}
	const std::size_t displayCount = m_bCSVTableViewOrderIdentity
		? m_csvTableModel->RowCount() : m_vCSVTableViewRows.size();
	ListView_SetItemCountEx(m_hwndCSVTable, static_cast<int>(displayCount),
		LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
	UpdateCSVSortIndicator();
	::InvalidateRect(m_hwndCSVTable, nullptr, TRUE);
	SendStatusMessage((L"表示件数 " + std::to_wstring(displayCount) + L" 件 / 全件数 " +
		std::to_wstring(m_csvTableModel->RowCount()) + L" 件").c_str());
	if (m_nCSVSortPendingSourceRow >= 0) {
		const std::size_t sourceRow = static_cast<std::size_t>(m_nCSVSortPendingSourceRow);
		m_nCSVSortPendingSourceRow = -1;
		int displayRow = -1;
		if (m_bCSVTableViewOrderIdentity) {
			displayRow = static_cast<int>(sourceRow);
		} else {
			const auto it = std::find(m_vCSVTableViewRows.begin(), m_vCSVTableViewRows.end(), sourceRow);
			if (it != m_vCSVTableViewRows.end()) displayRow = static_cast<int>(it - m_vCSVTableViewRows.begin());
		}
		ListView_SetItemState(m_hwndCSVTable, -1, 0, LVIS_FOCUSED | LVIS_SELECTED);
		if (displayRow >= 0) {
			ListView_SetItemState(m_hwndCSVTable, displayRow, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
			ListView_EnsureVisible(m_hwndCSVTable, displayRow, FALSE);
		}
	}
	if (m_nCSVPendingEditRow >= 0 && m_nCSVPendingEditColumn > 0) {
		const std::size_t sourceRow = static_cast<std::size_t>(m_nCSVPendingEditRow);
		int displayRow = -1;
		if (m_bCSVTableViewOrderIdentity) {
			displayRow = static_cast<int>(sourceRow);
		} else {
			const auto it = std::find(m_vCSVTableViewRows.begin(), m_vCSVTableViewRows.end(), sourceRow);
			if (it != m_vCSVTableViewRows.end()) displayRow = static_cast<int>(it - m_vCSVTableViewRows.begin());
		}
		const int column = m_nCSVPendingEditColumn;
		m_nCSVPendingEditRow = m_nCSVPendingEditColumn = -1;
		if (displayRow >= 0) {
			ListView_SetItemState(m_hwndCSVTable, displayRow, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
			ListView_EnsureVisible(m_hwndCSVTable, displayRow, FALSE);
			BeginCSVCellEdit(displayRow, column);
		}
	}
	if (m_uCSVValidationPendingOffset != static_cast<std::size_t>(-1)) {
		const std::size_t textOffset = m_uCSVValidationPendingOffset;
		m_uCSVValidationPendingOffset = static_cast<std::size_t>(-1);
		GoToCSVValidationDiagnostic(textOffset);
	}
}

void CEditWnd::ShowCSVFilterMenu(std::size_t column, POINT screenPoint)
{
	if (!m_bCSVTableMode || !m_csvTableModel || column >= m_csvTableModel->ColumnCount()) return;
	enum : UINT {
		FilterContains = 1, FilterNotContains, FilterEquals, FilterStarts, FilterEnds,
		FilterRegex, FilterNumberGreater, FilterNumberLess, FilterNumberEqual,
		FilterEmpty, FilterNonEmpty, FilterClearColumn, FilterClearAll, FilterToggleLogic,
		SortString, SortNumeric, SortDate, SortNatural, SortRemoveColumn, SortClearAll
	};
	HMENU menu = ::CreatePopupMenu();
	if (!menu) return;
	const std::wstring title = m_csvTableModel->ColumnTitle(column);
	const std::wstring titleLine = L"列「" + title + L"」";
	::AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, titleLine.c_str());
	::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(menu, MF_STRING, FilterContains, L"次を含む...");
	::AppendMenuW(menu, MF_STRING, FilterNotContains, L"次を含まない...");
	::AppendMenuW(menu, MF_STRING, FilterEquals, L"完全一致...");
	::AppendMenuW(menu, MF_STRING, FilterStarts, L"次で始まる...");
	::AppendMenuW(menu, MF_STRING, FilterEnds, L"次で終わる...");
	::AppendMenuW(menu, MF_STRING, FilterRegex, L"正規表現...");
	::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(menu, MF_STRING, FilterNumberGreater, L"数値が次より大きい...");
	::AppendMenuW(menu, MF_STRING, FilterNumberLess, L"数値が次より小さい...");
	::AppendMenuW(menu, MF_STRING, FilterNumberEqual, L"数値が次と等しい...");
	::AppendMenuW(menu, MF_STRING, FilterEmpty, L"空欄");
	::AppendMenuW(menu, MF_STRING, FilterNonEmpty, L"空欄以外");
	::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(menu, MF_STRING, FilterClearColumn, L"この列の条件を解除");
	::AppendMenuW(menu, MF_STRING, FilterClearAll, L"すべての条件を解除");
	::AppendMenuW(menu, MF_STRING, FilterToggleLogic,
		m_csvFilterLogic == csvtable::FilterLogic::Or
			? L"複数条件を AND で結合" : L"複数条件を OR で結合");
	::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"並べ替え（Shift+クリックで複数列）");
	::AppendMenuW(menu, MF_STRING, SortString, L"文字列の昇順で並べ替え");
	::AppendMenuW(menu, MF_STRING, SortNumeric, L"数値の昇順で並べ替え");
	::AppendMenuW(menu, MF_STRING, SortDate, L"日付の昇順で並べ替え");
	::AppendMenuW(menu, MF_STRING, SortNatural, L"自然順の昇順で並べ替え");
	::AppendMenuW(menu, MF_STRING, SortRemoveColumn, L"この列を並べ替え条件から外す");
	::AppendMenuW(menu, MF_STRING, SortClearAll, L"並べ替えを解除");
	const auto existing = std::find_if(m_vCSVFilters.begin(), m_vCSVFilters.end(),
		[column](const csvtable::Filter& filter) { return filter.column == column; });
	::EnableMenuItem(menu, FilterClearColumn, MF_BYCOMMAND |
		(existing == m_vCSVFilters.end() ? MF_GRAYED : MF_ENABLED));
	::EnableMenuItem(menu, FilterClearAll, MF_BYCOMMAND |
		(m_vCSVFilters.empty() ? MF_GRAYED : MF_ENABLED));
	::CheckMenuItem(menu, FilterToggleLogic, MF_BYCOMMAND |
		(m_csvFilterLogic == csvtable::FilterLogic::Or ? MF_CHECKED : MF_UNCHECKED));
	const UINT command = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
		screenPoint.x, screenPoint.y, 0, GetHwnd(), nullptr);
	::DestroyMenu(menu);
	if (command >= SortString && command <= SortClearAll) {
		if (command == SortClearAll) {
			m_vCSVSortKeys.clear();
		} else if (command == SortRemoveColumn) {
			m_vCSVSortKeys.erase(std::remove_if(m_vCSVSortKeys.begin(), m_vCSVSortKeys.end(),
				[column](const csvtable::SortKey& key) { return key.column == column; }), m_vCSVSortKeys.end());
		} else {
			const csvtable::SortKind kind = command == SortNumeric ? csvtable::SortKind::Numeric
				: command == SortDate ? csvtable::SortKind::Date
				: command == SortNatural ? csvtable::SortKind::Natural : csvtable::SortKind::String;
			auto key = std::find_if(m_vCSVSortKeys.begin(), m_vCSVSortKeys.end(),
				[column](const csvtable::SortKey& item) { return item.column == column; });
			if (key == m_vCSVSortKeys.end()) {
				m_vCSVSortKeys.insert(m_vCSVSortKeys.begin(), { column, kind, false });
			} else {
				csvtable::SortKey updated = *key;
				updated.kind = kind;
				updated.descending = false;
				m_vCSVSortKeys.erase(key);
				m_vCSVSortKeys.insert(m_vCSVSortKeys.begin(), updated);
			}
		}
		const int focusedRow = ListView_GetNextItem(m_hwndCSVTable, -1, LVNI_FOCUSED);
		m_nCSVSortPendingSourceRow = focusedRow >= 0
			? static_cast<int>(CSVSourceRow(static_cast<std::size_t>(focusedRow))) : -1;
		m_nCSVPendingEditRow = m_nCSVPendingEditColumn = -1;
		UpdateCSVSortIndicator();
		ScheduleCSVTableQuery();
		return;
	}

	csvtable::Filter filter;
	filter.column = column;
	bool needsValue = true;
	switch (command) {
	case FilterContains: filter.op = csvtable::FilterOperator::Contains; break;
	case FilterNotContains: filter.op = csvtable::FilterOperator::NotContains; break;
	case FilterEquals: filter.op = csvtable::FilterOperator::Equals; break;
	case FilterStarts: filter.op = csvtable::FilterOperator::StartsWith; break;
	case FilterEnds: filter.op = csvtable::FilterOperator::EndsWith; break;
	case FilterRegex: filter.op = csvtable::FilterOperator::Regex; break;
	case FilterNumberGreater: filter.op = csvtable::FilterOperator::NumberGreater; break;
	case FilterNumberLess: filter.op = csvtable::FilterOperator::NumberLess; break;
	case FilterNumberEqual: filter.op = csvtable::FilterOperator::NumberEqual; break;
	case FilterEmpty: filter.op = csvtable::FilterOperator::Empty; needsValue = false; break;
	case FilterNonEmpty: filter.op = csvtable::FilterOperator::NonEmpty; needsValue = false; break;
	case FilterClearColumn:
		m_vCSVFilters.erase(std::remove_if(m_vCSVFilters.begin(), m_vCSVFilters.end(),
			[column](const csvtable::Filter& item) { return item.column == column; }), m_vCSVFilters.end());
		break;
	case FilterClearAll:
		m_vCSVFilters.clear();
		break;
	case FilterToggleLogic:
		m_csvFilterLogic = m_csvFilterLogic == csvtable::FilterLogic::And
			? csvtable::FilterLogic::Or : csvtable::FilterLogic::And;
		break;
	default:
		return;
	}
	if (needsValue && command != FilterClearColumn && command != FilterClearAll &&
		command != FilterToggleLogic) {
		WCHAR value[512]{};
		if (existing != m_vCSVFilters.end()) {
			wcsncpy_s(value, existing->value.c_str(), _TRUNCATE);
		}
		CDlgInput1 dialog;
		if (!dialog.DoModal(CEditApp::getInstance()->GetAppInstance(), GetHwnd(),
			L"CSVフィルター", L"フィルター値を入力してください。",
			_countof(value) - 1, value)) return;
		filter.value = value;
		m_vCSVFilters.erase(std::remove_if(m_vCSVFilters.begin(), m_vCSVFilters.end(),
			[column](const csvtable::Filter& item) { return item.column == column; }), m_vCSVFilters.end());
		m_vCSVFilters.push_back(std::move(filter));
	} else if (!needsValue) {
		m_vCSVFilters.erase(std::remove_if(m_vCSVFilters.begin(), m_vCSVFilters.end(),
			[column](const csvtable::Filter& item) { return item.column == column; }), m_vCSVFilters.end());
		m_vCSVFilters.push_back(std::move(filter));
	}
	const int focusedRow = ListView_GetNextItem(m_hwndCSVTable, -1, LVNI_FOCUSED);
	m_nCSVSortPendingSourceRow = focusedRow >= 0
		? static_cast<int>(CSVSourceRow(static_cast<std::size_t>(focusedRow))) : -1;
	m_nCSVPendingEditRow = m_nCSVPendingEditColumn = -1;
	ScheduleCSVTableQuery();
	SendStatusMessage((L"CSV フィルター: " + std::to_wstring(m_vCSVFilters.size()) + L" 列").c_str());
}

void CEditWnd::ShowCSVColumnMenuForCurrentColumn()
{
	if (!m_bCSVTableMode || !m_csvTableModel || m_csvTableModel->ColumnCount() == 0) {
		SendStatusMessage(L"CSV テーブル表示に切り替えてください。");
		return;
	}
	const int column = std::clamp(m_nCSVCurrentColumn, 1,
		static_cast<int>(m_csvTableModel->ColumnCount())) - 1;
	POINT screenPoint{};
	::GetCursorPos(&screenPoint);
	ShowCSVFilterMenu(static_cast<std::size_t>(column), screenPoint);
}

void CEditWnd::UpdateCSVSelectionStatistics()
{
	if (!m_bCSVTableMode || !m_hwndCSVTable || !m_csvTableModel || m_nCSVCurrentColumn <= 0) return;
	const int selectedCount = ListView_GetSelectedCount(m_hwndCSVTable);
	if (selectedCount <= 0) {
		const std::size_t displayCount = m_bCSVTableViewOrderIdentity
			? m_csvTableModel->RowCount() : m_vCSVTableViewRows.size();
		SendStatusMessage((L"表示件数 " + std::to_wstring(displayCount) + L" 件 / 全件数 " +
			std::to_wstring(m_csvTableModel->RowCount()) + L" 件").c_str());
		return;
	}
	std::vector<csvtable::CellPosition> cells;
	try {
		cells.reserve(static_cast<std::size_t>(selectedCount));
		for (int displayRow = ListView_GetNextItem(m_hwndCSVTable, -1, LVNI_SELECTED);
			displayRow >= 0;
			displayRow = ListView_GetNextItem(m_hwndCSVTable, displayRow, LVNI_SELECTED)) {
			const std::size_t sourceRow = CSVSourceRow(static_cast<std::size_t>(displayRow));
			if (sourceRow < m_csvTableModel->RowCount()) {
				cells.push_back({ sourceRow, static_cast<std::size_t>(m_nCSVCurrentColumn - 1) });
			}
		}
		const csvtable::StatisticsResult result = csvtable::CalculateStatistics(*m_csvTableModel, cells);
		std::wstring message = L"選択 " + std::to_wstring(result.selectedCount) + L" 件 / 数値 " +
			std::to_wstring(result.numericCount) + L" 件";
		if (result.sum) {
			message += L" / 合計 " + std::to_wstring(*result.sum) + L" / 平均 " +
				std::to_wstring(*result.average) + L" / 最大 " + std::to_wstring(*result.maximum) +
				L" / 最小 " + std::to_wstring(*result.minimum);
		}
		SendStatusMessage(message.c_str());
	} catch (...) {
		SendStatusMessage(L"選択範囲の CSV 統計を計算できませんでした。");
	}
}

bool CEditWnd::ApplyCSVEditBatch(csvtable::TextEditBatch patches, int pendingEditRow, int pendingEditColumn)
{
	FinishCSVCellEdit(true);
	CEditDoc* document = GetDocument();
	if (!m_bCSVTableMode || !document || !m_csvTableModel || !m_csvTableSnapshot ||
		m_csvTableModel->DocumentVersion() != document->m_uCurlSyntaxVersion) {
		SendStatusMessage(L"CSV テーブルの状態が更新されました。再度操作してください。");
		return false;
	}
	if (patches.empty()) {
		SendStatusMessage(L"この CSV では指定した行または列を変更できませんでした。");
		return false;
	}
	const std::wstring_view source = m_csvTableSnapshot->Text();
	std::sort(patches.begin(), patches.end(), [](const auto& lhs, const auto& rhs) {
		return lhs.begin < rhs.begin;
	});
	std::size_t previousEnd = 0;
	for (const auto& patch : patches) {
		if (!patch.valid || patch.documentVersion != m_csvTableModel->DocumentVersion() ||
			patch.begin > source.size() || patch.deleteLength > source.size() - patch.begin ||
			patch.begin < previousEnd || patch.replacement.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
			SendStatusMessage(L"CSV の編集範囲を安全に特定できませんでした。");
			return false;
		}
		previousEnd = patch.begin + patch.deleteLength;
	}
	if (patches.size() > 1) {
		if (source.size() > CSV_BATCH_EDIT_MAX_SOURCE_CHARS || patches.size() > CSV_BATCH_EDIT_MAX_RECORDS) {
			SendStatusMessage(L"大きな CSV では複数行一括編集を中止しました。原文を保護するためテキスト表示で編集してください。");
			return false;
		}
		try {
			const std::size_t begin = patches.front().begin;
			const std::size_t end = patches.back().begin + patches.back().deleteLength;
			std::wstring replacement;
			replacement.reserve(end - begin);
			std::size_t cursor = begin;
			for (const auto& patch : patches) {
				replacement.append(source.substr(cursor, patch.begin - cursor));
				replacement.append(patch.replacement);
				cursor = patch.begin + patch.deleteLength;
			}
			replacement.append(source.substr(cursor, end - cursor));
			csvtable::TextEditPatch merged;
			merged.begin = begin;
			merged.deleteLength = end - begin;
			merged.replacement = std::move(replacement);
			merged.valid = true;
			merged.documentVersion = m_csvTableModel->DocumentVersion();
			patches.assign(1, std::move(merged));
		} catch (...) {
			SendStatusMessage(L"CSV の一括編集用メモリを確保できませんでした。");
			return false;
		}
	}
	const auto& patch = patches.front();
	CLogicPoint logicFrom, logicTo;
	if (!CSVOffsetToLogicPoint(patch.begin, &logicFrom) ||
		!CSVOffsetToLogicPoint(patch.begin + patch.deleteLength, &logicTo)) {
		SendStatusMessage(L"CSV の編集位置を文書上で特定できませんでした。");
		return false;
	}
	try {
		CLayoutRange layoutRange;
		document->m_cLayoutMgr.LogicToLayout(CLogicRange(logicFrom, logicTo), &layoutRange);
		CEditView& view = GetActiveView();
		view.GetCaret().MoveCursor(layoutRange.GetFrom(), false);
		view.GetSelectionInfo().DisableSelectArea(false);
		if (logicFrom != logicTo) view.GetSelectionInfo().SetSelectArea(layoutRange);
		if (pendingEditRow >= 0 && pendingEditColumn > 0) {
			m_nCSVPendingEditRow = pendingEditRow;
			m_nCSVPendingEditColumn = pendingEditColumn;
		}
		view.GetCommander().HandleCommand(F_INSTEXT_W, true,
			reinterpret_cast<LPARAM>(patch.replacement.c_str()),
			static_cast<LPARAM>(patch.replacement.size()), TRUE, 0);
		return true;
	} catch (...) {
		m_nCSVPendingEditRow = m_nCSVPendingEditColumn = -1;
		SendStatusMessage(L"CSV の編集を適用できませんでした。文書内容を確認してください。");
		return false;
	}
}

void CEditWnd::ShowCSVTableContextMenu(POINT screenPoint, int displayRow, int subItem)
{
	if (!m_bCSVTableMode || !m_csvTableModel) return;
	enum : UINT { AddRow = 1, DeleteRow, AddColumn, DeleteColumn };
	HMENU menu = ::CreatePopupMenu();
	if (!menu) return;
	const auto& parsed = m_csvTableSnapshot->Parsed();
	const bool canEditColumns = !parsed.records.empty() && parsed.records.size() <= CSV_BATCH_EDIT_MAX_RECORDS &&
		m_csvTableSnapshot->Text().size() <= CSV_BATCH_EDIT_MAX_SOURCE_CHARS &&
		std::none_of(parsed.records.begin(), parsed.records.end(), [](const csvtable::Record& record) {
			return record.malformed;
		});
	::AppendMenuW(menu, MF_STRING, AddRow, L"行を追加");
	::AppendMenuW(menu, MF_STRING, DeleteRow, L"この行を削除");
	::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(menu, MF_STRING | (canEditColumns ? 0 : MF_GRAYED), AddColumn,
		canEditColumns ? L"列を追加" : L"列を追加（大きい CSV／不正行では無効）");
	::AppendMenuW(menu, MF_STRING | (canEditColumns ? 0 : MF_GRAYED), DeleteColumn,
		canEditColumns ? L"選択列を削除" : L"選択列を削除（大きい CSV／不正行では無効）");
	const std::size_t sourceRow = displayRow >= 0 ? CSVSourceRow(static_cast<std::size_t>(displayRow))
		: static_cast<std::size_t>(-1);
	::EnableMenuItem(menu, DeleteRow, MF_BYCOMMAND |
		(sourceRow < m_csvTableModel->RowCount() ? MF_ENABLED : MF_GRAYED));
	const int column = subItem > 0 ? subItem - 1 : m_nCSVCurrentColumn - 1;
	::EnableMenuItem(menu, DeleteColumn, MF_BYCOMMAND |
		(column >= 0 && static_cast<std::size_t>(column) < m_csvTableModel->ColumnCount() ? MF_ENABLED : MF_GRAYED));
	const UINT command = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
		screenPoint.x, screenPoint.y, 0, GetHwnd(), nullptr);
	::DestroyMenu(menu);
	if (command == 0) return;
	if (subItem > 0) m_nCSVCurrentColumn = subItem;
	if (command == AddRow) {
		const int newRow = static_cast<int>(m_csvTableModel->RowCount());
		ApplyCSVEditBatch({ m_csvTableModel->PrepareAppendRow({}) }, newRow, 1);
	} else if (command == DeleteRow && sourceRow < m_csvTableModel->RowCount()) {
		ApplyCSVEditBatch({ m_csvTableModel->PrepareDeleteRow(sourceRow) });
	} else if (command == AddColumn && canEditColumns) {
		ApplyCSVEditBatch(m_csvTableModel->PrepareAppendColumn());
	} else if (command == DeleteColumn && canEditColumns && column >= 0 &&
		static_cast<std::size_t>(column) < m_csvTableModel->ColumnCount()) {
		ApplyCSVEditBatch(m_csvTableModel->PrepareDeleteColumn(static_cast<std::size_t>(column)));
	}
}

void CEditWnd::ShowCSVTableEditMenu()
{
	if (!m_bCSVTableMode || !m_hwndCSVTable) {
		SendStatusMessage(L"CSV テーブル表示に切り替えてください。");
		return;
	}
	int displayRow = ListView_GetNextItem(m_hwndCSVTable, -1, LVNI_FOCUSED);
	if (displayRow < 0) displayRow = ListView_GetNextItem(m_hwndCSVTable, -1, LVNI_SELECTED);
	POINT screenPoint{};
	::GetCursorPos(&screenPoint);
	ShowCSVTableContextMenu(screenPoint, displayRow, m_nCSVCurrentColumn);
}

std::size_t CEditWnd::CSVSourceRow(std::size_t displayRow) const noexcept
{
	if (m_bCSVTableViewOrderIdentity) return displayRow;
	return displayRow < m_vCSVTableViewRows.size()
		? m_vCSVTableViewRows[displayRow]
		: static_cast<std::size_t>(-1);
}

void CEditWnd::UpdateCSVSortIndicator()
{
	if (!m_hwndCSVTable) return;
	HWND header = ListView_GetHeader(m_hwndCSVTable);
	if (!header) return;
	const int count = Header_GetItemCount(header);
	for (int index = 0; index < count; ++index) {
		HDITEMW item{};
		item.mask = HDI_FORMAT;
		if (!::SendMessageW(header, HDM_GETITEMW, index, reinterpret_cast<LPARAM>(&item))) continue;
		item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
		const auto sort = std::find_if(m_vCSVSortKeys.begin(), m_vCSVSortKeys.end(),
			[index](const csvtable::SortKey& key) { return key.column + 1 == static_cast<std::size_t>(index); });
		if (sort != m_vCSVSortKeys.end()) item.fmt |= sort->descending ? HDF_SORTDOWN : HDF_SORTUP;
		::SendMessageW(header, HDM_SETITEMW, index, reinterpret_cast<LPARAM>(&item));
	}
}

void CEditWnd::PopulateCSVTableColumns()
{
	if (!m_hwndCSVTable || !m_csvTableModel) return;
	const std::size_t columns = m_csvTableModel->ColumnCount();
	const std::size_t rows = m_csvTableModel->RowCount();
	if (columns >= static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
		rows > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		SendStatusMessage(L"行数または列数が多すぎるため CSV テーブル表示にできません。");
		return;
	}
	ListView_SetItemCountEx(m_hwndCSVTable, 0, LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
	HWND header = ListView_GetHeader(m_hwndCSVTable);
	int count = header ? Header_GetItemCount(header) : 0;
	while (count-- > 0) ListView_DeleteColumn(m_hwndCSVTable, count);
	std::wstring title = L"#";
	LVCOLUMNW column{};
	column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
	column.pszText = title.data();
	column.cx = DpiScaleX(58);
	column.iSubItem = 0;
	ListView_InsertColumn(m_hwndCSVTable, 0, &column);
	for (std::size_t index = 0; index < columns; ++index) {
		title = m_csvTableModel->ColumnTitle(index);
		column.pszText = title.data();
		const auto& savedWidths = csvtable::GetUserSettings().columnWidths;
		const int savedWidth = index < savedWidths.size() && savedWidths[index] >= 24
			? savedWidths[index] : 140;
		column.cx = DpiScaleX(savedWidth);
		column.iSubItem = static_cast<int>(index + 1);
		ListView_InsertColumn(m_hwndCSVTable, static_cast<int>(index + 1), &column);
	}
	ListView_SetItemCountEx(m_hwndCSVTable, static_cast<int>(rows), LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
	m_vCSVTableViewRows.clear();
	m_bCSVTableViewOrderIdentity = true;
	UpdateCSVSortIndicator();
	::InvalidateRect(m_hwndCSVTable, nullptr, TRUE);
}

void CEditWnd::AutoSizeCSVTableColumns()
{
	if (!m_bCSVTableMode || !m_hwndCSVTable || !m_csvTableModel || !m_csvTableSnapshot) {
		SendStatusMessage(L"CSV テーブル表示に切り替えてください。");
		return;
	}
	HDC dc = ::GetDC(m_hwndCSVTable);
	if (!dc) {
		SendStatusMessage(L"列幅を自動調整できませんでした。");
		return;
	}
	HFONT font = reinterpret_cast<HFONT>(::SendMessageW(m_hwndCSVTable, WM_GETFONT, 0, 0));
	HGDIOBJ previousFont = font ? ::SelectObject(dc, font) : nullptr;
	const auto measure = [dc](std::wstring_view value) {
		SIZE size{};
		const int length = static_cast<int>(std::min<std::size_t>(value.size(), 512));
		return ::GetTextExtentPoint32W(dc, value.data(), length, &size) ? size.cx : 0;
	};
	auto& widths = csvtable::GetUserSettings().columnWidths;
	const std::size_t columnCount = std::min<std::size_t>(m_csvTableModel->ColumnCount(), 512);
	const std::size_t sampleRows = std::min<std::size_t>(m_csvTableModel->RowCount(), 200);
	try {
		if (widths.size() < columnCount) widths.resize(columnCount, 0);
		for (std::size_t columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
			std::wstring title;
			const auto& records = m_csvTableSnapshot->Parsed().records;
			if (m_csvTableModel->HasHeader() && !records.empty()) {
				if (const csvtable::Field* field = m_csvTableSnapshot->Parsed().FieldAt(records.front(), columnIndex)) {
					if (field->rawLength <= 512) title = csvtable::DecodeField(m_csvTableSnapshot->Text(), *field);
					else title.assign(m_csvTableSnapshot->Text().substr(field->rawBegin, 512));
				}
			} else {
				title = m_csvTableModel->ColumnTitle(columnIndex);
			}
			std::replace(title.begin(), title.end(), L'\r', L' ');
			std::replace(title.begin(), title.end(), L'\n', L' ');
			int widthPixels = measure(title);
			for (std::size_t row = 0; row < sampleRows; ++row) {
				const csvtable::RawFieldRange range = m_csvTableModel->CellRawRange(row, columnIndex);
				if (!range.valid) continue;
				int cellWidth = 0;
				if (range.length <= 512) {
					std::wstring value = m_csvTableModel->CellText(row, columnIndex);
					std::replace(value.begin(), value.end(), L'\r', L' ');
					std::replace(value.begin(), value.end(), L'\n', L' ');
					cellWidth = measure(value);
				} else {
					std::wstring raw(m_csvTableSnapshot->Text().substr(range.begin, 512));
					std::replace(raw.begin(), raw.end(), L'\r', L' ');
					std::replace(raw.begin(), raw.end(), L'\n', L' ');
					cellWidth = measure(raw);
				}
				widthPixels = std::max(widthPixels, cellWidth);
			}
			const int width = static_cast<int>(std::clamp<LONG>(DpiUnscaleX(widthPixels + DpiScaleX(28)), 40L, 600L));
			widths[columnIndex] = width;
			ListView_SetColumnWidth(m_hwndCSVTable, static_cast<int>(columnIndex + 1), DpiScaleX(width));
		}
	} catch (...) {
		if (previousFont && previousFont != HGDI_ERROR) ::SelectObject(dc, previousFont);
		::ReleaseDC(m_hwndCSVTable, dc);
		SendStatusMessage(L"列幅の計算中にメモリを確保できませんでした。");
		return;
	}
	if (previousFont && previousFont != HGDI_ERROR) ::SelectObject(dc, previousFont);
	::ReleaseDC(m_hwndCSVTable, dc);
	SendStatusMessage((L"先頭 " + std::to_wstring(sampleRows) + L" 件から列幅を自動調整しました。").c_str());
}

bool CEditWnd::CSVOffsetToLogicPoint(std::size_t offset, CLogicPoint* point) const
{
	const CEditDoc* document = GetDocument();
	if (!document || !point || offset > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
	std::size_t consumed = 0;
	int lineNumber = 0;
	const CDocLine* lastLine = nullptr;
	for (const CDocLine* line = document->m_cDocLineMgr.GetDocLineTop(); line; line = line->GetNextLine()) {
		lastLine = line;
		const int length = static_cast<int>(line->GetLengthWithEOL());
		if (length < 0) return false;
		if (offset < consumed + static_cast<std::size_t>(length)) {
			*point = CLogicPoint(static_cast<int>(offset - consumed), lineNumber);
			return true;
		}
		consumed += static_cast<std::size_t>(length);
		++lineNumber;
	}
	if (!lastLine || offset != consumed || lineNumber == 0) return false;
	*point = CLogicPoint(static_cast<int>(lastLine->GetLengthWithEOL()), lineNumber - 1);
	return true;
}

void CEditWnd::BeginCSVCellEdit(int row, int column)
{
	if (!m_bCSVTableMode || !m_hwndCSVTable || row < 0 || column < 1) return;
	FinishCSVCellEdit(true);
	if (!m_csvTableModel || !m_csvTableSnapshot) return;
	if (!GetDocument() || m_csvTableModel->DocumentVersion() != GetDocument()->m_uCurlSyntaxVersion) return;
	const std::size_t dataColumn = static_cast<std::size_t>(column - 1);
	const std::size_t dataRow = CSVSourceRow(static_cast<std::size_t>(row));
	if (dataRow >= m_csvTableModel->RowCount() || dataColumn >= m_csvTableModel->ColumnCount()) return;
	const std::size_t recordIndex = m_csvTableModel->RecordIndexForRow(dataRow);
	const auto& parsed = m_csvTableSnapshot->Parsed();
	if (recordIndex >= parsed.records.size()) return;
	const csvtable::Field* field = parsed.FieldAt(parsed.records[recordIndex], dataColumn);
	if (!field || field->malformed) {
		SendStatusMessage(L"不正な CSV フィールドは表形式では編集できません。テキスト表示で修正してください。");
		return;
	}
	RECT rect{};
	if (!ListView_GetSubItemRect(m_hwndCSVTable, row, column, LVIR_BOUNDS, &rect)) return;
	std::wstring value;
	try {
		value = m_csvTableModel->CellText(dataRow, dataColumn);
	} catch (...) {
		SendStatusMessage(L"セル編集用のメモリを確保できませんでした。");
		return;
	}
	const bool multiline = value.find_first_of(L"\r\n") != std::wstring::npos;
	if (multiline) {
		RECT client{};
		::GetClientRect(m_hwndCSVTable, &client);
		rect.bottom = std::min<LONG>(rect.bottom + DpiScaleY(80), client.bottom);
	}
	DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
	if (multiline) editStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
	HWND edit = ::CreateWindowExW(0, L"EDIT", value.c_str(), editStyle,
		rect.left, rect.top, std::max<LONG>(1, rect.right - rect.left), std::max<LONG>(1, rect.bottom - rect.top),
		m_hwndCSVTable, nullptr, CEditApp::getInstance()->GetAppInstance(), nullptr);
	if (!edit) {
		SendStatusMessage(L"セル編集欄を作成できませんでした。");
		return;
	}
	m_hwndCSVCellEdit = edit;
	m_nCSVEditingRow = row;
	m_nCSVEditingColumn = column;
	::SendMessageW(edit, EM_SETLIMITTEXT, static_cast<WPARAM>(std::numeric_limits<int>::max()), 0);
	::SendMessageW(edit, WM_SETFONT, ::SendMessageW(m_hwndCSVTable, WM_GETFONT, 0, 0), FALSE);
	::SetWindowSubclass(edit, &CEditWnd::CSVCellEditProc, 1, reinterpret_cast<DWORD_PTR>(this));
	::SendMessageW(edit, EM_SETSEL, 0, -1);
	::SetFocus(edit);
}

void CEditWnd::FinishCSVCellEdit(bool commit)
{
	if (!m_hwndCSVCellEdit || m_bCSVFinishingCellEdit) return;
	m_bCSVFinishingCellEdit = true;
	HWND edit = m_hwndCSVCellEdit;
	const int row = m_nCSVEditingRow;
	const int column = m_nCSVEditingColumn;
	std::wstring value;
	if (commit) {
		try {
			const int length = ::GetWindowTextLengthW(edit);
			if (length >= 0) {
				value.resize(static_cast<std::size_t>(length) + 1);
				::GetWindowTextW(edit, value.data(), length + 1);
				value.resize(static_cast<std::size_t>(length));
			}
		} catch (...) {
			commit = false;
			SendStatusMessage(L"セル編集結果を確保できませんでした。変更を取り消しました。");
		}
	}
	::RemoveWindowSubclass(edit, &CEditWnd::CSVCellEditProc, 1);
	m_hwndCSVCellEdit = nullptr;
	m_nCSVEditingRow = m_nCSVEditingColumn = -1;
	::DestroyWindow(edit);
	if (commit && m_csvTableModel && m_csvTableSnapshot && row >= 0 && column > 0 && GetDocument() &&
		m_csvTableModel->DocumentVersion() == GetDocument()->m_uCurlSyntaxVersion) {
		try {
			const std::size_t dataColumn = static_cast<std::size_t>(column - 1);
			const std::size_t dataRow = CSVSourceRow(static_cast<std::size_t>(row));
			if (dataRow >= m_csvTableModel->RowCount()) {
				SendStatusMessage(L"表示中のセル位置が古いため、編集を適用しませんでした。");
			} else {
				const std::wstring oldValue = m_csvTableModel->CellText(dataRow, dataColumn);
				if (value != oldValue) {
					const auto patch = m_csvTableModel->PrepareCellEdit(dataRow, dataColumn, value);
					CLogicPoint logicFrom, logicTo;
					if (patch.valid && patch.replacement.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
						CSVOffsetToLogicPoint(patch.begin, &logicFrom) &&
						CSVOffsetToLogicPoint(patch.begin + patch.deleteLength, &logicTo)) {
						CLayoutRange layoutRange;
						GetDocument()->m_cLayoutMgr.LogicToLayout(CLogicRange(logicFrom, logicTo), &layoutRange);
						CEditView& view = GetActiveView();
						view.GetCaret().MoveCursor(layoutRange.GetFrom(), false);
						view.GetSelectionInfo().DisableSelectArea(false);
						if (logicFrom != logicTo) view.GetSelectionInfo().SetSelectArea(layoutRange);
						view.GetCommander().HandleCommand(F_INSTEXT_W, true,
							reinterpret_cast<LPARAM>(patch.replacement.c_str()),
							static_cast<LPARAM>(patch.replacement.size()), TRUE, 0);
					} else {
						SendStatusMessage(L"セルの原文範囲を特定できないため、編集を適用しませんでした。");
					}
				}
			}
		} catch (...) {
			SendStatusMessage(L"セル編集を適用できませんでした。テキスト表示へ戻して確認してください。");
		}
	} else if (commit && row >= 0 && column > 0) {
		SendStatusMessage(L"文書が更新されているため、古いセル編集を破棄しました。");
	}
	m_bCSVFinishingCellEdit = false;
}

LRESULT CALLBACK CEditWnd::CSVCellEditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
	[[maybe_unused]] UINT_PTR subclassId, DWORD_PTR referenceData)
{
	auto* owner = reinterpret_cast<CEditWnd*>(referenceData);
	if (message == WM_KEYDOWN && owner) {
		if (wParam == VK_ESCAPE) {
			owner->FinishCSVCellEdit(false);
			if (owner->m_hwndCSVTable) ::SetFocus(owner->m_hwndCSVTable);
			return 0;
		}
		if (wParam == VK_RETURN) {
			const bool multiline = (::GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_MULTILINE) != 0;
			const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
			if (!multiline || ctrl) {
				owner->FinishCSVCellEdit(true);
				if (owner->m_hwndCSVTable) ::SetFocus(owner->m_hwndCSVTable);
				return 0;
			}
		}
		if (wParam == VK_TAB) {
			const int row = owner->m_nCSVEditingRow;
			const std::size_t sourceRow = owner->CSVSourceRow(static_cast<std::size_t>(std::max(0, row)));
			const bool backwards = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
			int column = owner->m_nCSVEditingColumn + (backwards ? -1 : 1);
			if (column < 1) column = 1;
			if (owner->m_csvTableModel && column > static_cast<int>(owner->m_csvTableModel->ColumnCount()))
				column = static_cast<int>(owner->m_csvTableModel->ColumnCount());
			const std::uint64_t versionBefore = owner->GetDocument() ? owner->GetDocument()->m_uCurlSyntaxVersion : 0;
			owner->FinishCSVCellEdit(true);
			if (owner->m_hwndCSVTable && column > 0) {
				ListView_SetItemState(owner->m_hwndCSVTable, row, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
				ListView_EnsureVisible(owner->m_hwndCSVTable, row, FALSE);
				owner->m_nCSVCurrentColumn = column;
				if (owner->m_csvTableModel && owner->GetDocument() &&
					owner->m_csvTableModel->DocumentVersion() == owner->GetDocument()->m_uCurlSyntaxVersion) {
					owner->BeginCSVCellEdit(row, column);
				} else if (owner->GetDocument() && owner->GetDocument()->m_uCurlSyntaxVersion != versionBefore) {
					owner->m_nCSVPendingEditRow = static_cast<int>(sourceRow);
					owner->m_nCSVPendingEditColumn = column;
				}
			}
			return 0;
		}
	}
	const LRESULT result = ::DefSubclassProc(hwnd, message, wParam, lParam);
	if (message == WM_KILLFOCUS && owner && !owner->m_bCSVFinishingCellEdit) owner->FinishCSVCellEdit(true);
	return result;
}

void CEditWnd::UpdateCSVTableLayout()
{
	if (!m_bCSVTableMode || !m_hwndCSVTable || !m_cSplitterWnd.GetHwnd()) return;
	RECT rect{};
	if (!::GetWindowRect(m_cSplitterWnd.GetHwnd(), &rect)) return;
	::MapWindowPoints(nullptr, GetHwnd(), reinterpret_cast<POINT*>(&rect), 2);
	::SetWindowPos(m_hwndCSVTable, HWND_TOP, rect.left, rect.top,
		std::max<LONG>(0, rect.right - rect.left), std::max<LONG>(0, rect.bottom - rect.top),
		SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void CEditWnd::NotifyCurlSyntaxDocumentLoaded()
{
	NotifyCurlSyntaxContentChanged(GetDocument());
}

void CEditWnd::OnCurlSyntaxDocumentTypeChanged()
{
	NotifyCurlSyntaxContentChanged(GetDocument());
	if (!IsCurlSyntaxDocument()) {
		m_bCurlSyntaxErrorListVisible = false;
		if (m_hwndCurlSyntaxErrorSummary) ::ShowWindow(m_hwndCurlSyntaxErrorSummary, SW_HIDE);
		if (m_hwndCurlSyntaxErrorList) ::ShowWindow(m_hwndCurlSyntaxErrorList, SW_HIDE);
		RECT client{};
		::GetClientRect(GetHwnd(), &client);
		OnSize(SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
	}
	RefreshCurlSyntaxErrorList();
	m_cToolbar.UpdateToolbar();
}

void CEditWnd::SetCurlSyntaxRealtime(bool enabled)
{
	curlsyntax::SetRealtimeEnabled(enabled);
	if (!enabled) {
		::KillTimer(GetHwnd(), IDT_CURL_SYNTAX);
		if (m_pCurlSyntaxCheckService) m_pCurlSyntaxCheckService->Cancel();
		if (auto* document = GetDocument()) {
			++document->m_uCurlSyntaxVersion;
			document->m_bCurlSyntaxResultStale = true;
			document->m_vCurlSyntaxDiagnostics.clear();
		}
		RefreshCurlSyntaxErrorList();
		m_bCurlSyntaxErrorListVisible = false;
		if (m_hwndCurlSyntaxErrorSummary) ::ShowWindow(m_hwndCurlSyntaxErrorSummary, SW_HIDE);
		if (m_hwndCurlSyntaxErrorList) ::ShowWindow(m_hwndCurlSyntaxErrorList, SW_HIDE);
		HideCurlSyntaxTooltip();
		RECT client{};
		::GetClientRect(GetHwnd(), &client);
		OnSize(SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
		Views_RedrawAll();
		m_cToolbar.UpdateToolbar();
		return;
	}
	NotifyCurlSyntaxDocumentLoaded();
	if (IsCurlSyntaxDocument()) {
		::KillTimer(GetHwnd(), IDT_CURL_SYNTAX);
		RunCurlSyntaxCheck(false);
	}
	m_cToolbar.UpdateToolbar();
}

void CEditWnd::RunCurlSyntaxCheck(bool manual)
{
	CEditDoc* document = GetDocument();
	if (!document || (!manual && !curlsyntax::IsRealtimeEnabled()) || !IsCurlSyntaxDocument()) return;
	std::wstring snapshot;
	for (const CDocLine* line = document->m_cDocLineMgr.GetDocLineTop(); line; line = line->GetNextLine()) {
		const CLogicInt length = line->GetLengthWithEOL();
		if (length > CLogicInt(0)) snapshot.append(line->GetPtr(), static_cast<size_t>(static_cast<int>(length)));
	}
	if (!m_pCurlSyntaxCheckService) m_pCurlSyntaxCheckService = std::make_unique<curlsyntax::CheckService>();
	const HWND resultWindow = GetHwnd();
	m_pCurlSyntaxCheckService->Schedule(document->m_uCurlSyntaxDocumentId,
		document->m_uCurlSyntaxVersion, std::move(snapshot),
		[this, resultWindow](curlsyntax::CheckResult result) {
			{
				std::lock_guard<std::mutex> lock(m_curlSyntaxMailboxMutex);
				m_curlSyntaxMailbox = std::move(result);
			}
			if (!::PostMessage(resultWindow, MYWM_CURL_SYNTAX_RESULT, 0, 0)) {
				std::lock_guard<std::mutex> lock(m_curlSyntaxMailboxMutex);
				m_curlSyntaxMailbox.reset();
			}
		}, true, manual);
}

void CEditWnd::RunCurlSyntaxCheckNow()
{
	RunCurlSyntaxCheck(true);
}

void CEditWnd::AcceptCurlSyntaxResult()
{
	std::optional<curlsyntax::CheckResult> result;
	{
		std::lock_guard<std::mutex> lock(m_curlSyntaxMailboxMutex);
		result.swap(m_curlSyntaxMailbox);
	}
	CEditDoc* document = GetDocument();
	if (!result || !document || (!curlsyntax::IsRealtimeEnabled() && !result->manual) || !IsCurlSyntaxDocument()) return;
	if (result->documentId != document->m_uCurlSyntaxDocumentId
		|| result->version != document->m_uCurlSyntaxVersion) return;
	document->m_vCurlSyntaxDiagnostics = std::move(result->diagnostics);
	document->m_bCurlSyntaxResultStale = false;
	RefreshCurlSyntaxErrorList();
	if (!document->m_vCurlSyntaxDiagnostics.empty()) {
		CreateCurlSyntaxErrorList();
		m_bCurlSyntaxErrorListVisible = m_hwndCurlSyntaxErrorList != nullptr;
		if (m_bCurlSyntaxErrorListVisible) {
			if (m_hwndCurlSyntaxErrorSummary) ::ShowWindow(m_hwndCurlSyntaxErrorSummary, SW_SHOW);
			::ShowWindow(m_hwndCurlSyntaxErrorList, SW_SHOW);
			RECT client{};
			::GetClientRect(GetHwnd(), &client);
			OnSize(SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
		}
	} else {
		SendStatusMessage(L"構文チェック完了, エラーはありません。");
	}
	Views_RedrawAll();
}

void CEditWnd::CreateCurlSyntaxErrorList()
{
	if (m_hwndCurlSyntaxErrorList) return;
	INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES };
	::InitCommonControlsEx(&controls);
	m_hwndCurlSyntaxErrorSummary = ::CreateWindowExW(0, L"STATIC", L"",
		WS_CHILD | SS_LEFT | SS_CENTERIMAGE,
		0, 0, 0, 0, GetHwnd(), nullptr, CEditApp::getInstance()->GetAppInstance(), nullptr);
	m_hwndCurlSyntaxErrorList = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
		WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
		0, 0, 0, 0, GetHwnd(), nullptr, CEditApp::getInstance()->GetAppInstance(), nullptr);
	if (!m_hwndCurlSyntaxErrorList) {
		if (m_hwndCurlSyntaxErrorSummary) {
			::DestroyWindow(m_hwndCurlSyntaxErrorSummary);
			m_hwndCurlSyntaxErrorSummary = nullptr;
		}
		return;
	}
	if (m_hwndCurlSyntaxErrorSummary) {
		::SendMessageW(m_hwndCurlSyntaxErrorSummary, WM_SETFONT,
			reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), FALSE);
	}
	::SetWindowSubclass(m_hwndCurlSyntaxErrorList, &CEditWnd::CurlSyntaxErrorListProc, 1, reinterpret_cast<DWORD_PTR>(this));
	ListView_SetExtendedListViewStyle(m_hwndCurlSyntaxErrorList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
	const wchar_t* titles[] = { L"種別", L"ファイル名", L"行", L"列", L"コード", L"メッセージ" };
	const int widths[] = { 78, 180, 58, 58, 92, 360 };
	for (int i = 0; i < 6; ++i) {
		LVCOLUMNW column{};
		column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		column.pszText = const_cast<LPWSTR>(titles[i]);
		column.cx = DpiScaleX(widths[i]);
		column.iSubItem = i;
		ListView_InsertColumn(m_hwndCurlSyntaxErrorList, i, &column);
	}
	RefreshCurlSyntaxErrorList();
}

LRESULT CALLBACK CEditWnd::CurlSyntaxErrorListProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
	UINT_PTR subclassId, DWORD_PTR refData)
{
	auto* owner = reinterpret_cast<CEditWnd*>(refData);
	if (!owner) return ::DefSubclassProc(hwnd, message, wParam, lParam);
	const int resizeGrip = DpiScaleY(6);
	if (message == WM_LBUTTONDOWN && GET_Y_LPARAM(lParam) <= resizeGrip) {
		owner->m_bCurlSyntaxErrorListDragging = true;
		::SetCapture(hwnd);
		::SetCursor(::LoadCursor(nullptr, IDC_SIZENS));
		return 0;
	}
	if (message == WM_MOUSEMOVE && GET_Y_LPARAM(lParam) <= resizeGrip) {
		::SetCursor(::LoadCursor(nullptr, IDC_SIZENS));
	}
	if (message == WM_MOUSEMOVE && owner->m_bCurlSyntaxErrorListDragging) {
		POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		::MapWindowPoints(hwnd, owner->GetHwnd(), &point, 1);
		RECT client{};
		::GetClientRect(owner->GetHwnd(), &client);
		int contentBottom = client.bottom;
		if (HWND status = owner->m_cStatusBar.GetStatusHwnd()) {
			RECT statusRect{};
			::GetWindowRect(status, &statusRect);
			::MapWindowPoints(nullptr, owner->GetHwnd(), reinterpret_cast<POINT*>(&statusRect), 2);
			contentBottom = statusRect.top;
		}
		const int minHeight = DpiScaleY(80);
		const int maxHeight = std::max<int>(minHeight, static_cast<int>((contentBottom - DpiScaleY(100)) / 2));
		owner->m_nCurlSyntaxErrorListHeight = DpiUnscaleY(std::clamp<int>(contentBottom - point.y, minHeight, maxHeight));
		owner->OnSize(SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
		::SetCursor(::LoadCursor(nullptr, IDC_SIZENS));
		return 0;
	}
	if (message == WM_LBUTTONUP && owner->m_bCurlSyntaxErrorListDragging) {
		owner->m_bCurlSyntaxErrorListDragging = false;
		if (::GetCapture() == hwnd) ::ReleaseCapture();
		curlsyntax::SetErrorListHeight(owner->m_nCurlSyntaxErrorListHeight);
		return 0;
	}
	if (message == WM_CAPTURECHANGED) owner->m_bCurlSyntaxErrorListDragging = false;
	if (message == WM_NCDESTROY) ::RemoveWindowSubclass(hwnd, &CEditWnd::CurlSyntaxErrorListProc, subclassId);
	return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

void CEditWnd::RefreshCurlSyntaxErrorList()
{
	if (!m_hwndCurlSyntaxErrorList && !m_hwndCurlSyntaxErrorSummary) return;
	const CEditDoc* document = GetDocument();
	if (m_bCSVValidationListActive) {
		std::size_t errors = 0;
		std::size_t warnings = 0;
		for (const auto& diagnostic : m_vCSVValidationDiagnostics) {
			if (diagnostic.severity == csvtable::Severity::Error) ++errors;
			else ++warnings;
		}
		if (m_hwndCurlSyntaxErrorSummary) {
			std::wstring summary = m_bCSVValidationStale
				? L"CSVチェック中 / 文書変更後は再チェックが必要です"
				: L"CSV エラー " + std::to_wstring(errors) + L" 件 / 警告 " + std::to_wstring(warnings) + L" 件";
			::SetWindowTextW(m_hwndCurlSyntaxErrorSummary, summary.c_str());
		}
		if (!m_hwndCurlSyntaxErrorList) return;
		ListView_DeleteAllItems(m_hwndCurlSyntaxErrorList);
		if (m_bCSVValidationStale) {
			LVITEMW item{};
			item.mask = LVIF_TEXT;
			item.iItem = 0;
			item.pszText = const_cast<LPWSTR>(L"文書が変更されたため、CSVチェック結果は古くなっています。再チェックしてください。");
			ListView_InsertItem(m_hwndCurlSyntaxErrorList, &item);
			return;
		}
		if (m_vCSVValidationDiagnostics.empty()) {
			LVITEMW item{};
			item.mask = LVIF_TEXT;
			item.iItem = 0;
			item.pszText = const_cast<LPWSTR>(L"CSV エラーはありません");
			ListView_InsertItem(m_hwndCurlSyntaxErrorList, &item);
			return;
		}
		const std::wstring fileName = document ? document->m_cDocFile.GetFileName() : L"";
		for (std::size_t i = 0; i < m_vCSVValidationDiagnostics.size(); ++i) {
			const auto& diagnostic = m_vCSVValidationDiagnostics[i];
			const std::wstring severity = diagnostic.severity == csvtable::Severity::Error ? L"Error" : L"Warning";
			const std::wstring location = L"R" + std::to_wstring(diagnostic.record) +
				L" / L" + std::to_wstring(diagnostic.physicalLine);
			const std::wstring column = std::to_wstring(diagnostic.column) +
				(diagnostic.columnName.empty() ? L"" : L" (" + diagnostic.columnName + L")");
			LVITEMW item{};
			item.mask = LVIF_TEXT | LVIF_PARAM;
			item.iItem = static_cast<int>(i);
			item.lParam = static_cast<LPARAM>(diagnostic.textOffset);
			item.pszText = const_cast<LPWSTR>(severity.c_str());
			const int index = ListView_InsertItem(m_hwndCurlSyntaxErrorList, &item);
			if (index < 0) continue;
			ListView_SetItemText(m_hwndCurlSyntaxErrorList, index, 1, const_cast<LPWSTR>(fileName.c_str()));
			ListView_SetItemText(m_hwndCurlSyntaxErrorList, index, 2, const_cast<LPWSTR>(location.c_str()));
			ListView_SetItemText(m_hwndCurlSyntaxErrorList, index, 3, const_cast<LPWSTR>(column.c_str()));
			ListView_SetItemText(m_hwndCurlSyntaxErrorList, index, 4, const_cast<LPWSTR>(diagnostic.code.c_str()));
			ListView_SetItemText(m_hwndCurlSyntaxErrorList, index, 5, const_cast<LPWSTR>(diagnostic.message.c_str()));
		}
		return;
	}
	if (m_hwndCurlSyntaxErrorSummary) {
		std::wstring summary;
		if (document && document->m_bCurlSyntaxResultStale) {
			summary = L"構文チェック中";
		} else {
			const auto errorCount = document ? std::count_if(document->m_vCurlSyntaxDiagnostics.begin(),
				document->m_vCurlSyntaxDiagnostics.end(), [](const auto& diagnostic) {
					return diagnostic.severity == curlsyntax::Severity::Error;
				}) : 0;
			summary = L"エラー合計: " + std::to_wstring(errorCount) + L" 件";
		}
		::SetWindowTextW(m_hwndCurlSyntaxErrorSummary, summary.c_str());
	}
	if (!m_hwndCurlSyntaxErrorList) return;
	ListView_DeleteAllItems(m_hwndCurlSyntaxErrorList);
	if (!document || document->m_vCurlSyntaxDiagnostics.empty()) {
		LVITEMW item{};
		item.mask = LVIF_TEXT;
		item.iItem = 0;
		item.pszText = const_cast<LPWSTR>(L"構文エラーはありません");
		ListView_InsertItem(m_hwndCurlSyntaxErrorList, &item);
		return;
	}
	const std::wstring fileName = document->m_cDocFile.GetFileName();
	for (size_t i = 0; i < document->m_vCurlSyntaxDiagnostics.size(); ++i) {
		const auto& diagnostic = document->m_vCurlSyntaxDiagnostics[i];
		const std::wstring severity = diagnostic.severity == curlsyntax::Severity::Error ? L"Error" : L"Warning";
		const std::wstring row = std::to_wstring(diagnostic.line);
		const std::wstring column = std::to_wstring(diagnostic.column);
		const std::wstring code(diagnostic.code.begin(), diagnostic.code.end());
		LVITEMW item{};
		item.mask = LVIF_TEXT | LVIF_PARAM;
		item.iItem = static_cast<int>(i);
		item.lParam = static_cast<LPARAM>(i);
		item.pszText = const_cast<LPWSTR>(severity.c_str());
		const int index = ListView_InsertItem(m_hwndCurlSyntaxErrorList, &item);
		ListView_SetItemText(m_hwndCurlSyntaxErrorList, index, 1, const_cast<LPWSTR>(fileName.c_str()));
		ListView_SetItemText(m_hwndCurlSyntaxErrorList, index, 2, const_cast<LPWSTR>(row.c_str()));
		ListView_SetItemText(m_hwndCurlSyntaxErrorList, index, 3, const_cast<LPWSTR>(column.c_str()));
		ListView_SetItemText(m_hwndCurlSyntaxErrorList, index, 4, const_cast<LPWSTR>(code.c_str()));
		ListView_SetItemText(m_hwndCurlSyntaxErrorList, index, 5, const_cast<LPWSTR>(diagnostic.message.c_str()));
	}
}

void CEditWnd::ToggleCurlSyntaxErrorList()
{
	CreateCurlSyntaxErrorList();
	if (!m_hwndCurlSyntaxErrorList) return;
	m_bCurlSyntaxErrorListVisible = !m_bCurlSyntaxErrorListVisible;
	if (m_hwndCurlSyntaxErrorSummary) ::ShowWindow(m_hwndCurlSyntaxErrorSummary, m_bCurlSyntaxErrorListVisible ? SW_SHOW : SW_HIDE);
	::ShowWindow(m_hwndCurlSyntaxErrorList, m_bCurlSyntaxErrorListVisible ? SW_SHOW : SW_HIDE);
	RECT client{};
	::GetClientRect(GetHwnd(), &client);
	OnSize(SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
}

void CEditWnd::GoToCurlSyntaxDiagnostic(size_t index)
{
	CEditDoc* document = GetDocument();
	if (!document || document->m_bCurlSyntaxResultStale || index >= document->m_vCurlSyntaxDiagnostics.size()) return;
	const auto& diagnostic = document->m_vCurlSyntaxDiagnostics[index];
	CLayoutPoint layoutPosition;
	document->m_cLayoutMgr.LogicToLayout(CLogicPoint(diagnostic.column - 1, diagnostic.line - 1), &layoutPosition);
	GetActiveView().GetCaret().MoveCursor(layoutPosition, true);
	GetActiveView().GetCaret().m_nCaretPosX_Prev = layoutPosition.x;
	GetActiveView().Redraw();
}

void CEditWnd::GoToCSVValidationDiagnostic(std::size_t textOffset)
{
	CEditDoc* document = GetDocument();
	if (!document || m_bCSVValidationStale || m_uCSVValidationVersion != document->m_uCurlSyntaxVersion) return;
	if (m_bCSVTableMode && m_csvTableModel && m_hwndCSVTable) {
		const std::size_t displayRows = m_bCSVTableViewOrderIdentity
			? m_csvTableModel->RowCount() : m_vCSVTableViewRows.size();
		for (std::size_t row = 0; row < displayRows; ++row) {
			const std::size_t sourceRow = CSVSourceRow(row);
			const std::size_t recordIndex = m_csvTableModel->RecordIndexForRow(sourceRow);
			const auto& parsed = m_csvTableSnapshot->Parsed();
			if (recordIndex >= parsed.records.size()) continue;
			const auto& record = parsed.records[recordIndex];
			if (textOffset >= record.rawBegin && textOffset <= record.rawBegin + record.rawLength) {
				ListView_SetItemState(m_hwndCSVTable, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
				ListView_SetItemState(m_hwndCSVTable, static_cast<int>(row), LVIS_SELECTED | LVIS_FOCUSED,
					LVIS_SELECTED | LVIS_FOCUSED);
				ListView_EnsureVisible(m_hwndCSVTable, static_cast<int>(row), FALSE);
				if (!m_vCSVValidationDiagnostics.empty()) {
					for (const auto& diagnostic : m_vCSVValidationDiagnostics) {
						if (diagnostic.textOffset == textOffset) {
							m_nCSVCurrentColumn = static_cast<int>(diagnostic.column);
							break;
						}
					}
				}
				::SetFocus(m_hwndCSVTable);
				return;
			}
		}
		if (!m_vCSVFilters.empty()) {
			m_uCSVValidationPendingOffset = textOffset;
			m_nCSVSortPendingSourceRow = -1;
			m_vCSVFilters.clear();
			ScheduleCSVTableQuery();
			if (m_vCSVSortKeys.empty()) {
				m_uCSVValidationPendingOffset = static_cast<std::size_t>(-1);
				GoToCSVValidationDiagnostic(textOffset);
			}
			return;
		}
	}
	CLogicPoint logic;
	if (!CSVOffsetToLogicPoint(textOffset, &logic)) return;
	CLayoutPoint layoutPosition;
	document->m_cLayoutMgr.LogicToLayout(logic, &layoutPosition);
	GetActiveView().GetCaret().MoveCursor(layoutPosition, true);
	GetActiveView().GetCaret().m_nCaretPosX_Prev = layoutPosition.x;
	GetActiveView().Redraw();
}

void CEditWnd::NavigateAutomaticMarker(bool forward)
{
	CEditDoc* document = GetDocument();
	const auto* marker = CWordMarker::Instance().AutomaticWord(document);
	if (!document || !marker || marker->empty()) return;

	const CLogicPoint caret = GetActiveView().GetCaret().GetCaretLogicPos();
	const int currentLine = static_cast<int>(caret.y);
	const int currentColumn = static_cast<int>(caret.x);
	int targetLine = -1;
	int targetColumn = -1;
	int firstLine = -1;
	int firstColumn = -1;
	int lastLine = -1;
	int lastColumn = -1;
	int lineNumber = 0;
	for (const CDocLine* line = document->m_cDocLineMgr.GetDocLineTop(); line; line = line->GetNextLine(), ++lineNumber) {
		const int length = static_cast<int>(line->GetLengthWithoutEOL());
		const std::wstring_view text = length > 0
			? std::wstring_view(line->GetPtr(), static_cast<size_t>(length))
			: std::wstring_view{};
		size_t position = 0;
		while ((position = text.find(std::wstring_view(*marker), position)) != std::wstring_view::npos) {
			const int column = static_cast<int>(position);
			if (firstLine < 0) { firstLine = lineNumber; firstColumn = column; }
			lastLine = lineNumber; lastColumn = column;
			const bool afterCaret = lineNumber > currentLine || (lineNumber == currentLine && column > currentColumn);
			const bool beforeCaret = lineNumber < currentLine || (lineNumber == currentLine && column < currentColumn);
			if (forward && afterCaret && targetLine < 0) { targetLine = lineNumber; targetColumn = column; }
			if (!forward && beforeCaret) { targetLine = lineNumber; targetColumn = column; }
			++position;
		}
	}
	if (targetLine < 0) {
		targetLine = forward ? firstLine : lastLine;
		targetColumn = forward ? firstColumn : lastColumn;
	}
	if (targetLine < 0) return;
	CLayoutPoint layoutPosition;
	document->m_cLayoutMgr.LogicToLayout(CLogicPoint(targetColumn, targetLine), &layoutPosition);
	CLayoutPoint layoutEnd;
	document->m_cLayoutMgr.LogicToLayout(
		CLogicPoint(targetColumn + static_cast<int>(marker->size()), targetLine), &layoutEnd);
	auto& view = GetActiveView();
	view.GetCaret().MoveCursor(layoutPosition, true);
	view.GetCaret().m_nCaretPosX_Prev = layoutPosition.x;
	view.GetSelectionInfo().SetSelectArea(CLayoutRange(layoutPosition, layoutEnd));
	view.GetSelectionInfo().DrawSelectArea();
	view.Redraw();
}

void CEditWnd::NavigateCurlSyntaxError(bool forward)
{
	const CEditDoc* document = GetDocument();
	if (!document || document->m_bCurlSyntaxResultStale || document->m_vCurlSyntaxDiagnostics.empty()) return;
	const CLogicPoint current = GetActiveView().GetCaret().GetCaretLogicPos();
	size_t target = forward ? 0 : document->m_vCurlSyntaxDiagnostics.size() - 1;
	if (forward) {
		for (size_t i = 0; i < document->m_vCurlSyntaxDiagnostics.size(); ++i) {
			const auto& d = document->m_vCurlSyntaxDiagnostics[i];
			if (d.line - 1 > static_cast<int>(current.y) || (d.line - 1 == static_cast<int>(current.y) && d.column - 1 > static_cast<int>(current.x))) { target = i; break; }
		}
	} else {
		for (size_t i = document->m_vCurlSyntaxDiagnostics.size(); i-- > 0;) {
			const auto& d = document->m_vCurlSyntaxDiagnostics[i];
			if (d.line - 1 < static_cast<int>(current.y) || (d.line - 1 == static_cast<int>(current.y) && d.column - 1 < static_cast<int>(current.x))) { target = i; break; }
		}
	}
	GoToCurlSyntaxDiagnostic(target);
}

void CEditWnd::UpdateCurlSyntaxHover(const CLayoutPoint& layoutPosition, POINT screenPosition, bool foldGutterHover)
{
	const CEditDoc* document = GetDocument();
	std::wstring tooltipText;
	if (document && foldGutterHover && document->m_cLayoutMgr.IsCodeFoldSupported()) {
		const CLayout* row = document->m_cLayoutMgr.SearchLineByLayoutY(layoutPosition.y);
		if (row && row->GetLogicOffset() == CLogicInt(0)
			&& document->m_cLayoutMgr.IsCodeFoldCollapsedAtLine(row->GetLogicLineNo())) {
			if (const auto* region = document->m_cLayoutMgr.FindCodeFoldAtLine(row->GetLogicLineNo())) {
				tooltipText = L"折りたたみ: " + region->kind + L"（" +
					std::to_wstring(document->m_cLayoutMgr.GetCodeFoldHiddenLineCount(row->GetLogicLineNo())) + L" 行）";
				const size_t previewEnd = region->firstLine + (std::min)(size_t(3), region->lastLine - region->firstLine);
				for (size_t lineNo = region->firstLine + 1; lineNo <= previewEnd; ++lineNo) {
					const CDocLine* line = document->m_cDocLineMgr.GetLine(CLogicInt(static_cast<Int>(lineNo)));
					if (!line) continue;
					const size_t previewLength = static_cast<size_t>((std::max)(0, static_cast<Int>(line->GetLengthWithoutEOL())));
					std::wstring_view preview;
					if (previewLength > 0) preview = std::wstring_view(line->GetPtr(), previewLength);
					while (!preview.empty() && std::iswspace(preview.front())) preview.remove_prefix(1);
					if (preview.size() > 96) preview = preview.substr(0, 96);
					tooltipText += L"\n";
					tooltipText.append(preview);
				}
			}
		}
	}
	const curlsyntax::Diagnostic* found = nullptr;
	if (tooltipText.empty() && document && !document->m_bCurlSyntaxResultStale && IsCurlSyntaxDocument()) {
		CLogicPoint logicPosition;
		document->m_cLayoutMgr.LayoutToLogic(layoutPosition, &logicPosition);
		const int line = static_cast<int>(logicPosition.y) + 1;
		const int column = static_cast<int>(logicPosition.x) + 1;
		for (const auto& diagnostic : document->m_vCurlSyntaxDiagnostics) {
			if (diagnostic.line == line && (diagnostic.column == column || static_cast<int>(layoutPosition.x) <= 0)) { found = &diagnostic; break; }
		}
	}
	if (tooltipText.empty() && !found) {
		HideCurlSyntaxTooltip();
		return;
	}
	if (!m_hwndCurlSyntaxTooltip) {
		m_hwndCurlSyntaxTooltip = ::CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
			WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
			GetHwnd(), nullptr, CEditApp::getInstance()->GetAppInstance(), nullptr);
		if (!m_hwndCurlSyntaxTooltip) return;
		TOOLINFOW tool{};
		tool.cbSize = sizeof(tool);
		tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
		tool.hwnd = GetHwnd();
		tool.uId = 1;
		tool.lpszText = const_cast<LPWSTR>(L"");
		::SendMessageW(m_hwndCurlSyntaxTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
	}
	if (tooltipText.empty()) {
		m_strCurlSyntaxTooltip = found->message + L"\n行：" + std::to_wstring(found->line) +
			L"\n列：" + std::to_wstring(found->column) + L"\nエラーコード：" +
			std::wstring(found->code.begin(), found->code.end());
	} else {
		m_strCurlSyntaxTooltip = std::move(tooltipText);
	}
	TOOLINFOW tool{};
	tool.cbSize = sizeof(tool);
	tool.hwnd = GetHwnd();
	tool.uId = 1;
	tool.lpszText = m_strCurlSyntaxTooltip.data();
	::SendMessageW(m_hwndCurlSyntaxTooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
	::SendMessageW(m_hwndCurlSyntaxTooltip, TTM_TRACKPOSITION, 0,
		MAKELONG(screenPosition.x + DpiScaleX(12), screenPosition.y + DpiScaleY(18)));
	::SendMessageW(m_hwndCurlSyntaxTooltip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
}

void CEditWnd::HideCurlSyntaxTooltip()
{
	if (m_hwndCurlSyntaxTooltip) {
		TOOLINFOW tool{};
		tool.cbSize = sizeof(tool);
		tool.hwnd = GetHwnd();
		tool.uId = 1;
		::SendMessageW(m_hwndCurlSyntaxTooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
	}
}

//! ドキュメントリスナ：セーブ後
// 2008.02.02 kobake
void CEditWnd::OnAfterSave([[maybe_unused]] const SSaveInfo& sSaveInfo)
{
	//ビュー再描画
	this->Views_RedrawAll();

	//キャプションの更新を行う
	UpdateCaption();

	/* キャレットの行桁位置を表示する */
	GetActiveView().GetCaret().ShowCaretPosInfo();
}

void CEditWnd::UpdateCaption()
{
	if( !GetActiveView().GetDrawSwitch() )return;

	const  CommonSetting& Common = GetDllShareData().m_Common;

	const auto pszWindowCaptionFormat = IsActiveApp()
		? Common.m_sWindow.m_szWindowCaptionActive
		: Common.m_sWindow.m_szWindowCaptionInactive;

	const auto pszTabCaptionFormat = Common.m_sTabBar.m_szTabWndCaption;

	wchar_t	pszCap[1024];

	//キャプション更新
	CSakuraEnvironment::ExpandParameter( pszWindowCaptionFormat, pszCap, int(std::size(pszCap)) );
	::SetWindowText( GetHwnd(), pszCap );

	//タブウインドウのファイル名を通知
	CSakuraEnvironment::ExpandParameter( pszTabCaptionFormat, pszCap, int(std::size(pszCap)) );
	ChangeFileNameNotify( pszCap,
		GetListeningDoc()->m_cDocFile.GetFilePath(),
		CEditApp::getInstance()->m_pcGrepAgent->m_bGrepMode ); // 2006.01.28 ryoji ファイル名、Grepモードパラメータを追加
}

//!< ウィンドウ生成用の矩形を取得
void CEditWnd::_GetWindowRectForInit(
	CMyRect* rcResult,
	int nGroup [[maybe_unused]],
	const STabGroupInfo& sTabGroupInfo
) const
{
	/* ウィンドウサイズ継承 */
	int	nWinCX, nWinCY;
	//	2004.05.13 Moca m_Common.m_eSaveWindowSizeをBOOLからenumに変えたため
	if( WINSIZEMODE_DEF != m_pShareData->m_Common.m_sWindow.m_eSaveWindowSize ){
		nWinCX = m_pShareData->m_Common.m_sWindow.m_nWinSizeCX;
		nWinCY = m_pShareData->m_Common.m_sWindow.m_nWinSizeCY;
	}else{
		nWinCX = CW_USEDEFAULT;
		nWinCY = 0;
	}

	/* ウィンドウサイズ指定 */
	EditInfo fi;
	CCommandLine::getInstance()->GetEditInfo(&fi);
	if( fi.m_nWindowSizeX >= 0 ){
		nWinCX = fi.m_nWindowSizeX;
	}
	if( fi.m_nWindowSizeY >= 0 ){
		nWinCY = fi.m_nWindowSizeY;
	}

	/* ウィンドウ位置指定 */
	int nWinOX, nWinOY;
	nWinOX = CW_USEDEFAULT;
	nWinOY = 0;
	// ウィンドウ位置固定
	//	2004.05.13 Moca 保存したウィンドウ位置を使う場合は共有メモリからセット
	if( WINSIZEMODE_DEF != m_pShareData->m_Common.m_sWindow.m_eSaveWindowPos ){
		nWinOX =  m_pShareData->m_Common.m_sWindow.m_nWinPosX;
		nWinOY =  m_pShareData->m_Common.m_sWindow.m_nWinPosY;
	}

	//	2004.05.13 Moca マルチディスプレイでは負の値も有効なので，
	//	未設定の判定方法を変更．(負の値→CW_USEDEFAULT)
	if( fi.m_nWindowOriginX != CW_USEDEFAULT ){
		nWinOX = fi.m_nWindowOriginX;
	}
	if( fi.m_nWindowOriginY != CW_USEDEFAULT ){
		nWinOY = fi.m_nWindowOriginY;
	}

	// 必要なら、タブグループにフィットするよう、変更
	if(sTabGroupInfo.IsValid()){
		RECT rcWork, rcMon;
		GetMonitorWorkRect( sTabGroupInfo.hwndTop, &rcWork, &rcMon );

		const WINDOWPLACEMENT& wpTop = sTabGroupInfo.wpTop;
		nWinCX = wpTop.rcNormalPosition.right  - wpTop.rcNormalPosition.left;
		nWinCY = wpTop.rcNormalPosition.bottom - wpTop.rcNormalPosition.top;
		nWinOX = wpTop.rcNormalPosition.left   + (rcWork.left - rcMon.left);
		nWinOY = wpTop.rcNormalPosition.top    + (rcWork.top - rcMon.top);
	}

	//結果
	rcResult->SetXYWH(nWinOX,nWinOY,nWinCX,nWinCY);
}

HWND CEditWnd::_CreateMainWindow(int nGroup, const STabGroupInfo& sTabGroupInfo)
{
	// -- -- -- -- ウィンドウクラス登録 -- -- -- -- //
	WNDCLASSEX	wc;
	//	Apr. 27, 2000 genta
	//	サイズ変更時のちらつきを抑えるためCS_HREDRAW | CS_VREDRAW を外した
	wc.style			= CS_DBLCLKS | CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW;
	wc.lpfnWndProc		= CEditWndProc;
	wc.cbClsExtra		= 0;
	wc.cbWndExtra		= sizeof(LONG_PTR) * 1;                                  //拡張領域を1個確保。
	wc.hInstance		= G_AppInstance();
	//	Dec, 2, 2002 genta アイコン読み込み方法変更
	wc.hIcon			= GetAppIcon( G_AppInstance(), ICON_DEFAULT_APP, FN_APP_ICON, false );

	wc.hCursor			= nullptr/*LoadCursor( NULL, IDC_ARROW )*/;
	wc.hbrBackground	= (HBRUSH)nullptr/*(COLOR_3DSHADOW + 1)*/;
	wc.lpszMenuName		= nullptr;	// MAKEINTRESOURCE( IDR_MENU1 );	2010/5/16 Uchi
	wc.lpszClassName	= GSTR_EDITWINDOWNAME;

	//	Dec. 6, 2002 genta
	//	small icon指定のため RegisterClassExに変更
	wc.cbSize			= sizeof( wc );
	wc.hIconSm			= GetAppIcon( G_AppInstance(), ICON_DEFAULT_APP, FN_APP_ICON, true );
	ATOM	atom = RegisterClassEx( &wc );
	if( 0 == atom ){
		//	2004.05.13 Moca return NULLを有効にした
		return nullptr;
	}

	//矩形取得
	CMyRect rc;
	_GetWindowRectForInit(&rc, nGroup, sTabGroupInfo);

	//作成
	HWND hwndResult = ::CreateWindowEx(
		0,				 	// extended window style
		GSTR_EDITWINDOWNAME,		// pointer to registered class name
		GSTR_EDITWINDOWNAME,		// pointer to window name
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,	// window style
		rc.left,			// horizontal position of window
		rc.top,				// vertical position of window
		rc.Width(),			// window width
		rc.Height(),		// window height
		nullptr,				// handle to parent or owner window
		nullptr,				// handle to menu or child-window identifier
		G_AppInstance(),		// handle to application instance
		nullptr				// pointer to window-creation data
	);
	return hwndResult;
}

void CEditWnd::_GetTabGroupInfo(
	STabGroupInfo* pTabGroupInfo,
	int& nGroup
) const
{
	HWND hwndTop = nullptr;
	WINDOWPLACEMENT	wpTop = {0};

	//From Here @@@ 2003.05.31 MIK
	//タブウインドウの場合は現状値を指定
	if( m_pShareData->m_Common.m_sTabBar.m_bDispTabWnd && !m_pShareData->m_Common.m_sTabBar.m_bDispTabWndMultiWin )
	{
		if( nGroup < 0 )	// 不正なグループID
			nGroup = 0;	// グループ指定無し（最近アクティブのグループに入れる）
		EditNode*	pEditNode = CAppNodeGroupHandle(nGroup).GetEditNodeAt(0);	// グループの先頭ウィンドウ情報を取得	// 2007.06.20 ryoji
		hwndTop = pEditNode? pEditNode->GetHwnd(): nullptr;

		if( hwndTop )
		{
			//	Sep. 11, 2003 MIK 新規TABウィンドウの位置が上にずれないように
			// 2007.06.20 ryoji 非プライマリモニタまたはタスクバーを動かした後でもずれないように

			wpTop.length = sizeof(wpTop);
			if( ::GetWindowPlacement( hwndTop, &wpTop ) ){	// 現在の先頭ウィンドウから位置を取得
				if( wpTop.showCmd == SW_SHOWMINIMIZED )
					wpTop.showCmd = pEditNode->m_showCmdRestore;
			}
			else{
				hwndTop = nullptr;
			}
		}
	}
	//To Here @@@ 2003.05.31 MIK

	//結果
	pTabGroupInfo->hwndTop = hwndTop;
	pTabGroupInfo->wpTop = wpTop;
}

void CEditWnd::_AdjustInMonitor(const STabGroupInfo& sTabGroupInfo)
{
	RECT	rcOrg;
	RECT	rcDesktop;
//	int		nWork;

	//	May 01, 2004 genta マルチモニタ対応
	::GetMonitorWorkRect( GetHwnd(), &rcDesktop );
	::GetWindowRect( GetHwnd(), &rcOrg );

	// 2005.11.23 Moca マルチモニタ等で問題があったため計算方法変更
	/* ウィンドウ位置調整 */
	if( rcOrg.bottom > rcDesktop.bottom ){
		rcOrg.top -= rcOrg.bottom - rcDesktop.bottom;
		rcOrg.bottom = rcDesktop.bottom;	//@@@ 2002.01.08
	}
	if( rcOrg.right > rcDesktop.right ){
		rcOrg.left -= rcOrg.right - rcDesktop.right;
		rcOrg.right = rcDesktop.right;	//@@@ 2002.01.08
	}

	if( rcOrg.top < rcDesktop.top ){
		rcOrg.bottom += rcDesktop.top - rcOrg.top;
		rcOrg.top = rcDesktop.top;
	}
	if( rcOrg.left < rcDesktop.left ){
		rcOrg.right += rcDesktop.left - rcOrg.left;
		rcOrg.left = rcDesktop.left;
	}

	/* ウィンドウサイズ調整 */
	if( rcOrg.bottom > rcDesktop.bottom ){
		//rcOrg.bottom = rcDesktop.bottom - 1;	//@@@ 2002.01.08
		rcOrg.bottom = rcDesktop.bottom;	//@@@ 2002.01.08
	}
	if( rcOrg.right > rcDesktop.right ){
		//rcOrg.right = rcDesktop.right - 1;	//@@@ 2002.01.08
		rcOrg.right = rcDesktop.right;	//@@@ 2002.01.08
	}

	//From Here @@@ 2003.06.13 MIK
	if( m_pShareData->m_Common.m_sTabBar.m_bDispTabWnd
		&& !m_pShareData->m_Common.m_sTabBar.m_bDispTabWndMultiWin
		&& sTabGroupInfo.hwndTop )
	{
		// 現在の先頭ウィンドウから WS_EX_TOPMOST 状態を引き継ぐ	// 2007.05.18 ryoji
		DWORD dwExStyle = (DWORD)::GetWindowLongPtr( sTabGroupInfo.hwndTop, GWL_EXSTYLE );
		::SetWindowPos( GetHwnd(), (dwExStyle & WS_EX_TOPMOST)? HWND_TOPMOST: HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );

		//タブウインドウ時は現状を維持
		{
			// 初回表示のアニメーション効果を抑止する

			// 先頭ウィンドウの背後で画面描画してから手前に出す（ツールバーやビューのちらつきを抑える）
			// ここでは、あとで正式に適用されるはずのドキュメントタイプを仮設定して一時描画しておく（ビューの配色切替によるちらつきを抑える）
			// さらに、タイプを戻して画面を無効化だけしておく（何らかの原因で途中停止した場合にはもとのタイプ色で再描画されるように ← 例えばファイルサイズが大きすぎる警告を出すときなど）
			// ※ 正攻法とはいえないかもしれないがあちこち手を入れることなく簡潔に済ませられるのでこうしておく
			CTypeConfig cTypeOld, cTypeNew(-1);
			cTypeOld = GetDocument()->m_cDocType.GetDocumentType();	// 現在のタイプ
			{
				EditInfo ei, mruei;
				CCommandLine::getInstance()->GetEditInfo( &ei );
				if( ei.m_szDocType[0] != '\0' ){
					cTypeNew = CDocTypeManager().GetDocumentTypeOfExt( ei.m_szDocType );
				}else{
					if( CMRUFile().GetEditInfo( ei.m_szPath, &mruei ) && 0 < mruei.m_nTypeId ){
						cTypeNew = CDocTypeManager().GetDocumentTypeOfId(mruei.m_nTypeId);
					}
					if( !cTypeNew.IsValidType() ){
						if( ei.m_szPath[0] ){
							cTypeNew = CDocTypeManager().GetDocumentTypeOfPath( ei.m_szPath );
						}else{
							cTypeNew = cTypeOld;
						}
					}
				}
			}
			GetDocument()->m_cDocType.SetDocumentType( cTypeNew, true, true );	// 仮設定

			// 可能な限り画面描画の様子が見えないよう一時的に先頭ウィンドウの後ろに配置
			::SetWindowPos( GetHwnd(), sTabGroupInfo.hwndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );

			// アニメーション効果は一時的に OFF にする
			ANIMATIONINFO ai = {sizeof(ANIMATIONINFO)};
			::SystemParametersInfo( SPI_GETANIMATION, sizeof(ANIMATIONINFO), &ai, 0 );
			int iMinAnimateOld = ai.iMinAnimate;
			ai.iMinAnimate = 0;
			::SystemParametersInfo( SPI_SETANIMATION, sizeof(ANIMATIONINFO), &ai, 0 );

			// 可視化する（最大化のときは次の ::ShowWindow() で手前に出てしまうので、アニメーション除去効果はあるがクライアント領域のちらつきは抑えきれない）
			int nCmdShow = ( sTabGroupInfo.wpTop.showCmd == SW_SHOWMAXIMIZED )? SW_SHOWMAXIMIZED: SW_SHOWNOACTIVATE;
			::ShowWindow( GetHwnd(), nCmdShow );
			::UpdateWindow( GetHwnd() );	// 画面更新
			::BringWindowToTop( GetHwnd() );
			::ShowWindowAsync(sTabGroupInfo.hwndTop, SW_HIDE );	// 以前の先頭ウィンドウはここで消しておかないと消えるアニメーションが見える場合がある

			// アニメーション効果を戻す
			ai.iMinAnimate = iMinAnimateOld;
			::SystemParametersInfo( SPI_SETANIMATION, sizeof(ANIMATIONINFO), &ai, 0 );

			// アイドリング開始時にその時点のタイプ別設定色で再描画されるようにしておく
			GetDocument()->m_cDocType.SetDocumentType( cTypeOld, true, true );	// タイプ戻し
			::InvalidateRect( GetHwnd(), nullptr, TRUE );	// 画面無効化
		}
	}
	else
	{
		::SetWindowPos(
			GetHwnd(), nullptr,
			rcOrg.left, rcOrg.top,
			rcOrg.right - rcOrg.left, rcOrg.bottom - rcOrg.top,
			SWP_NOOWNERZORDER | SWP_NOZORDER
		);

		/* ウィンドウサイズ継承 */
		if( WINSIZEMODE_DEF != m_pShareData->m_Common.m_sWindow.m_eSaveWindowSize &&
			m_pShareData->m_Common.m_sWindow.m_nWinSizeType == SIZE_MAXIMIZED ){
			::ShowWindow( GetHwnd(), SW_SHOWMAXIMIZED );
		}else
		// 2004.05.14 Moca ウィンドウサイズを直接指定する場合は、最小化表示を受け入れる
		if( WINSIZEMODE_SET == m_pShareData->m_Common.m_sWindow.m_eSaveWindowSize &&
			m_pShareData->m_Common.m_sWindow.m_nWinSizeType == SIZE_MINIMIZED ){
			::ShowWindow( GetHwnd(), SW_SHOWMINIMIZED );
		}
		else{
			::ShowWindow( GetHwnd(), SW_SHOW );
		}
	}
	//To Here @@@ 2003.06.13 MIK
}

/*!
	作成

	@date 2002.03.07 genta nDocumentType追加
	@date 2007.06.26 ryoji nGroup追加
	@date 2008.04.19 ryoji 初回アイドリング検出用ゼロ秒タイマーのセット処理を追加
*/
HWND CEditWnd::Create(
	[[maybe_unused]] const CEditDoc* pcEditDoc,
	CImageListMgr*	pcIcons,	//!< [in] Image List
	int				nGroup		//!< [in] グループID
)
{
	MY_RUNNINGTIMER( cRunningTimer, L"CEditWnd::Create" );

	wmemset( m_pszMenubarMessage, L' ', MENUBAR_MESSAGE_MAX_LEN );	// null終端は不要

	//	Dec. 4, 2002 genta
	InitMenubarMessageFont();

	// 2009.01.17 nasukoji	ホイールスクロール有無状態をクリア
	ClearMouseState();

	// ウィンドウ毎にアクセラレータテーブルを作成する
	CreateAccelTbl();

	//ウィンドウ数制限
	if( m_pShareData->m_sNodes.m_nEditArrNum >= MAX_EDITWINDOWS ){	//最大値修正	//@@@ 2003.05.31 MIK
		OkMessage( nullptr, LS(STR_MAXWINDOW), MAX_EDITWINDOWS );
		return nullptr;
	}

	//タブグループ情報取得
	STabGroupInfo sTabGroupInfo;
	_GetTabGroupInfo(&sTabGroupInfo, nGroup);

	// -- -- -- -- ウィンドウ作成 -- -- -- -- //
	HWND hWnd = _CreateMainWindow(nGroup, sTabGroupInfo);
	if(!hWnd)return nullptr;
	m_hWnd = hWnd;

	DarkMode::setDarkTitleBarEx(hWnd, true);

	// 初回アイドリング検出用のゼロ秒タイマーをセットする	// 2008.04.19 ryoji
	// ゼロ秒タイマーが発動（初回アイドリング検出）したら MYWM_FIRST_IDLE を起動元プロセスにポストする。
	// ※起動元での起動先アイドリング検出については CControlTray::OpenNewEditor を参照
	::SetTimer( GetHwnd(), IDT_FIRST_IDLE, 0, nullptr );

	/* 編集ウィンドウリストへの登録 */
	// 2011.01.12 ryoji この処理は以前はウィンドウ可視化よりも後の位置にあった
	// Vista/7 での初回表示アニメーション抑止（rev1868）とのからみで、ウィンドウが可視化される時点でタブバーに全タブが揃っていないと見苦しいのでここに移動。
	// AddEditWndList() で自ウィンドウにポストされる MYWM_TAB_WINDOW_NOTIFY(TWNT_ADD) はタブバー作成後の初回アイドリング時に処理されるので特に問題は無いはず。
	if( !CAppNodeGroupHandle(nGroup).AddEditWndList( GetHwnd() ) ){	// 2007.06.26 ryoji nGroup引数追加
		OkMessage( GetHwnd(), LS(STR_MAXWINDOW), MAX_EDITWINDOWS );
		::DestroyWindow( GetHwnd() );
		m_hWnd = hWnd = nullptr;
		return hWnd;
	}

	//コモンコントロール初期化
	MyInitCommonControls();

	//イメージ、ヘルパなどの作成
	m_cMenuDrawer.Create( G_AppInstance(), GetHwnd(), pcIcons );
	m_cToolbar.Create( pcIcons );

	// プラグインコマンドを登録する
	RegisterPluginCommand();

	SelectCharWidthCache( CWM_FONT_MINIMAP, CWM_CACHE_LOCAL ); // Init
	InitCharWidthCache( m_pcViewFontMiniMap->GetLogfont(), CWM_FONT_MINIMAP );
	SelectCharWidthCache( CWM_FONT_EDIT, GetLogfontCacheMode() );
	InitCharWidthCache( GetLogfont() );

	// -- -- -- -- 子ウィンドウ作成 -- -- -- -- //

	/* 分割フレーム作成 */
	m_cSplitterWnd.Create( GetHwnd() );

	/* ビュー */
	GetView(0).Create( m_cSplitterWnd.GetHwnd(), GetDocument(), 0, TRUE, false  );
	GetView(0).OnSetFocus();

	/* 子ウィンドウの設定 */
	HWND        hWndArr[2];
	hWndArr[0] = GetView(0).GetHwnd();
	hWndArr[1] = nullptr;
	m_cSplitterWnd.SetChildWndArr( hWndArr );

	MY_TRACETIME( cRunningTimer, L"View created" );

	// -- -- -- -- 各種バー作成 -- -- -- -- //

	// メインメニュー
	LayoutMainMenu();

	/* ツールバー */
	LayoutToolBar();

	/* ステータスバー */
	LayoutStatusBar();

	/* ファンクションキー バー */
	LayoutFuncKey();

	/* タブウインドウ */
	LayoutTabBar();

	// ミニマップ
	LayoutMiniMap();

	/* バーの配置終了 */
	EndLayoutBars( FALSE );

	DarkMode::setChildCtrlsTheme(hWnd);
	DarkMode::setWindowMenuBarSubclass(hWnd);
	DarkMode::setChildCtrlsSubclassAndTheme(hWnd);

	// -- -- -- -- その他調整など -- -- -- -- //

	// 画面表示直前にDispatchEventを有効化する
	::SetWindowLongPtr( GetHwnd(), GWLP_USERDATA, (LONG_PTR)this );

	// デスクトップからはみ出さないようにする
	_AdjustInMonitor(sTabGroupInfo);

	// ドロップされたファイルを受け入れる
	::DragAcceptFiles( GetHwnd(), TRUE );
	m_pcDropTarget->Register_DropTarget( m_hWnd );	// 右ボタンドロップ用	// 2008.06.20 ryoji

	//アクティブ情報
	m_bIsActiveApp = ( ::GetActiveWindow() == GetHwnd() );	// 2007.03.08 ryoji

	// PeekMessageの結果を受け取る構造体
	MSG msg{};

	// メッセージキューを作成する
	::PeekMessageW(&msg, hWnd, WM_USER, WM_USER, PM_NOREMOVE);

	// エディタ－トレイ間でのUI特権分離の確認（Vista UIPI機能） 2007.06.07 ryoji
	if (const auto hWndTray = m_pShareData->m_sHandles.m_hwndTray) {
		// 戻り値取得用変数（成功するとhWndが返って来る）
		DWORD_PTR dwRes = 0;

		// コントロールプロセスにMYWM_UIPI_CHECKを送る
		::SendMessageTimeoutW(hWndTray, MYWM_UIPI_CHECK, 0L, LPARAM(hWnd), SMTO_NORMAL, 10000, &dwRes);

		// メッセージ返送を回収する（とれない場合もあるが問題はない。）
		::PeekMessageW(&msg, hWnd, MYWM_UIPI_CHECK, MYWM_UIPI_CHECK, PM_REMOVE | PM_QS_SENDMESSAGE);

		if (!dwRes) {	// 送信失敗
			TopErrorMessage( GetHwnd(),
				LS(STR_ERR_DLGEDITWND02)
			);
			::DestroyWindow( GetHwnd() );
			m_hWnd = hWnd = nullptr;
			return hWnd;
		}
	}

	CShareData::getInstance()->SetTraceOutSource( GetHwnd() );	// TraceOut()起動元ウィンドウの設定	// 2006.06.26 ryoji

	//	Aug. 29, 2003 wmlhq
	m_nTimerCount = 0;
	/* タイマーを起動 */ // タイマーのIDと間隔を変更 20060128 aroka
	if( 0 == ::SetTimer( GetHwnd(), IDT_EDIT, 500, nullptr ) ){
		WarningMessage( GetHwnd(), LS(STR_ERR_DLGEDITWND03) );
	}
	// ツールバーのタイマーを分離した 20060128 aroka
	Timer_ONOFF( true );

	//デフォルトのIMEモード設定
	GetDocument()->m_cDocEditor.SetImeMode( GetDocument()->m_cDocType.GetDocumentAttribute().m_nImeState );

	return GetHwnd();
}

//! 起動時のファイルオープン処理
void CEditWnd::OpenDocumentWhenStart(
	const SLoadInfo& _sLoadInfo		//!< [in]
)
{
	if( _sLoadInfo.cFilePath.Length() ){
		::ShowWindow( GetHwnd(), SW_SHOW );
		//	Oct. 03, 2004 genta コード確認は設定に依存
		SLoadInfo	sLoadInfo = _sLoadInfo;
		bool		bReadResult = GetDocument()->m_cDocFileOperation.FileLoadWithoutAutoMacro(&sLoadInfo);	// 自動実行マクロは後で別の場所で実行される
		if( !bReadResult ){
			/* ファイルが既に開かれている */
			if( sLoadInfo.bOpened ){
				::PostMessageAny( GetHwnd(), WM_CLOSE, 0, 0 );
				// 2004.07.12 Moca return NULLだと、メッセージループを通らずにそのまま破棄されてしまい、タブの終了処理が抜ける
				//	この後は正常ルートでメッセージループに入った後WM_CLOSEを受信して直ちにCLOSE & DESTROYとなる．
				//	その中で編集ウィンドウの削除が行われる．
			}
		}
	}
}

void CEditWnd::SetDocumentTypeWhenCreate(
	ECodeType		nCharCode,		//!< [in] 漢字コード
	bool			bViewMode,		//!< [in] ビューモードで開くかどうか
	CTypeConfig		nDocumentType	//!< [in] 文書タイプ．-1のとき強制指定無し．
)
{
	//	Mar. 7, 2002 genta 文書タイプの強制指定
	//	Jun. 4 ,2004 genta ファイル名指定が無くてもタイプ強制指定を有効にする
	if( nDocumentType.IsValidType() ){
		GetDocument()->m_cDocType.SetDocumentType( nDocumentType, true );
		//	2002/05/07 YAZAKI タイプ別設定一覧の一時適用のコードを流用
		GetDocument()->m_cDocType.LockDocumentType();
	}

	// 文字コードの指定	2008/6/14 Uchi
	if( IsValidCodeType( nCharCode ) || nDocumentType.IsValidType() ){
		const STypeConfig& types = GetDocument()->m_cDocType.GetDocumentAttribute();
		ECodeType eDefaultCharCode = types.m_encoding.m_eDefaultCodetype;
		if( !IsValidCodeType( nCharCode ) ){
			nCharCode = eDefaultCharCode;	// 直接コード指定がなければタイプ指定のデフォルト文字コードを使用
		}
		if( nCharCode == eDefaultCharCode ){	// デフォルト文字コードと同じ文字コードが選択されたとき
			GetDocument()->SetDocumentEncoding( nCharCode, types.m_encoding.m_bDefaultBom );
			GetDocument()->m_cDocEditor.m_cNewLineCode = types.m_encoding.m_eDefaultEoltype;
		}
		else{
			GetDocument()->SetDocumentEncoding( nCharCode, CCodeTypeName( nCharCode ).IsBomDefOn() );
			GetDocument()->m_cDocEditor.m_cNewLineCode = EEolType::cr_and_lf;
		}
	}

	//	Jun. 4 ,2004 genta ファイル名指定が無くてもビューモード強制指定を有効にする
	CAppMode::getInstance()->SetViewMode(bViewMode);

	if( nDocumentType.IsValidType() ){
		/* 設定変更を反映させる */
		GetDocument()->OnChangeSetting();	// <--- 内部に BlockingHook() 呼び出しがあるので溜まった描画がここで実行される
	}
}

/*! メインメニューの配置処理
	@date 2010/05/16 Uchi
	@date 2012/10/18 syat 各国語対応
*/
void CEditWnd::LayoutMainMenu()
{
	WCHAR		szLabel[300];
	WCHAR		szKey[10];

	const auto pcMenu = &m_pShareData->m_Common.m_sMainMenu;

	HWND		hWnd = GetHwnd();
	int 		j;
	int 		nCount;
	LPCWSTR		pszName;

	const auto hMenu = ::CreateMenu();

	for (int i = 0; i < MAX_MAINMENU_TOP && pcMenu->m_nMenuTopIdx[i] >= 0; i++) {
		nCount = ( i >= MAX_MAINMENU_TOP || pcMenu->m_nMenuTopIdx[i+1] < 0 ? pcMenu->m_nMainMenuNum : pcMenu->m_nMenuTopIdx[i+1] )
				- pcMenu->m_nMenuTopIdx[i];		// メニュー項目数
		const auto cMainMenu = &pcMenu->m_cMainMenuTbl[pcMenu->m_nMenuTopIdx[i]];
		switch (cMainMenu->m_nType) {
		case T_NODE:
			// ラベル未設定かつFunctionコードがありならストリングテーブルから取得 2012/10/18 syat 各国語対応
			pszName = ( cMainMenu->m_sName[0] == L'\0' && cMainMenu->m_nFunc != F_NODE )
								? LS( cMainMenu->m_nFunc ) : cMainMenu->m_sName;
			::AppendMenu( hMenu, MF_POPUP | MF_STRING | (nCount<=1 ? MF_GRAYED : 0), (UINT_PTR)CreatePopupMenu(),
				CKeyBind::MakeMenuLabel( pszName, cMainMenu->m_sKey ) );
			break;
		case T_LEAF:
			/* メニューラベルの作成 */
			// 2014.05.04 Moca プラグイン/マクロ等を置けるようにFunccode2Nameを使うように
			GetDocument()->m_cFuncLookup.Funccode2Name( cMainMenu->m_nFunc, szLabel, int(std::size(szLabel)) );
			wcscpy( szKey, cMainMenu->m_sKey );
			if (CKeyBind::GetMenuLabel(
				G_AppInstance(),
				m_pShareData->m_Common.m_sKeyBind.m_nKeyNameArrNum,
				m_pShareData->m_Common.m_sKeyBind.m_pKeyNameArr,
				cMainMenu->m_nFunc,
				szLabel,
				cMainMenu->m_sKey,
				FALSE,
				int(std::size(szLabel))) == nullptr) {
				wcscpy( szLabel, L"?" );
			}
			::AppendMenu( hMenu, MF_STRING, cMainMenu->m_nFunc, szLabel );
			break;
		case T_SEPARATOR:
			::AppendMenu( hMenu, MF_SEPARATOR, 0, nullptr );
			break;
		case T_SPECIAL:
			nCount = 0;
			switch (cMainMenu->m_nFunc) {
			case F_WINDOW_LIST:				// ウィンドウリスト
				EditNode*	pEditNodeArr;
				nCount = CAppNodeManager::getInstance()->GetOpenedWindowArr( &pEditNodeArr, TRUE );
				delete [] pEditNodeArr;
				break;
			case F_FILE_USED_RECENTLY:		// 最近使ったファイル
				{
					CRecentFile	cRecentFile;
					nCount = cRecentFile.GetViewCount();
				}
				break;
			case F_FOLDER_USED_RECENTLY:	// 最近使ったフォルダー
				{
					CRecentFolder	cRecentFolder;
					nCount = cRecentFolder.GetViewCount();
				}
				break;
			case F_CUSTMENU_LIST:			// カスタムメニューリスト
				//	右クリックメニュー
				if (m_pShareData->m_Common.m_sCustomMenu.m_nCustMenuItemNumArr[0] > 0) {
					nCount++;
				}
				//	カスタムメニュー
				for (j = 1; j < MAX_CUSTOM_MENU; ++j) {
					if (m_pShareData->m_Common.m_sCustomMenu.m_nCustMenuItemNumArr[j] > 0) {
						nCount++;
					}
				}
				break;
			case F_USERMACRO_LIST:			// 登録済みマクロリスト
				for (j = 0; j < MAX_CUSTMACRO; ++j) {
					MacroRec *mp = &m_pShareData->m_Common.m_sMacro.m_MacroTable[j];
					if (mp->IsEnabled()) {
						nCount++;
					}
				}
				break;
			case F_PLUGIN_LIST:				// プラグインコマンドリスト
				//プラグインコマンドを提供するプラグインを列挙する
				{
					const CJackManager* pcJackManager = CJackManager::getInstance();

					CPlug::Array plugs = pcJackManager->GetPlugs( PP_COMMAND );
					for( CPlug::ArrayIter it = plugs.cbegin(); it != plugs.cend(); it++ ){
						nCount++;
					}
				}
				break;
			default:
				break;
			}
			::AppendMenu( hMenu, MF_POPUP | MF_STRING | (nCount<=0 ? MF_GRAYED : 0), (UINT_PTR)CreatePopupMenu(),
				CKeyBind::MakeMenuLabel( LS(cMainMenu->m_nFunc), cMainMenu->m_sKey ) );
			break;
		}
	}
	HMENU hMenuOld = ::GetMenu( hWnd );
	SetMenu( hWnd, hMenu );
	if( hMenuOld ){
		DestroyMenu( hMenuOld );
	}

	DarkMode::setWindowMenuBarSubclass(hWnd);
	DrawMenuBar( hWnd );
}

/*! ツールバーの配置処理
	@date 2006.12.19 ryoji 新規作成
*/
void CEditWnd::LayoutToolBar( void )
{
	if( m_pShareData->m_Common.m_sWindow.m_bDispTOOLBAR ){	/* ツールバーを表示する */
		m_cToolbar.CreateToolBar();
		m_cToolbar.UpdateToolbar();
	}else{
		m_cToolbar.DestroyToolBar();
	}
}

/*! ステータスバーの配置処理
	@date 2006.12.19 ryoji 新規作成
*/
void CEditWnd::LayoutStatusBar( void )
{
	if( m_pShareData->m_Common.m_sWindow.m_bDispSTATUSBAR ){	/* ステータスバーを表示する */
		/* ステータスバー作成 */
		m_cStatusBar.CreateStatusBar();
	}
	else{
		/* ステータスバー破棄 */
		m_cStatusBar.DestroyStatusBar();
	}
}

/*! ファンクションキーの配置処理
	@date 2006.12.19 ryoji 新規作成
*/
void CEditWnd::LayoutFuncKey( void )
{
	if( m_pShareData->m_Common.m_sWindow.m_bDispFUNCKEYWND ){	/* ファンクションキーを表示する */
		if( nullptr == m_cFuncKeyWnd.GetHwnd() ){
			bool	bSizeBox;
			if( m_pShareData->m_Common.m_sWindow.m_nFUNCKEYWND_Place == 0 ){	/* ファンクションキー表示位置／0:上 1:下 */
				bSizeBox = false;
			}else{
				bSizeBox = true;
				/* ステータスバーがあるときはサイズボックスを表示しない */
				if( m_cStatusBar.GetStatusHwnd() ){
					bSizeBox = false;
				}
			}
			m_cFuncKeyWnd.Open( G_AppInstance(), GetHwnd(), GetDocument(), bSizeBox );
		}
	}else{
		m_cFuncKeyWnd.Close();
	}
}

/*! タブバーの配置処理
	@date 2006.12.19 ryoji 新規作成
*/
void CEditWnd::LayoutTabBar( void )
{
	if( m_pShareData->m_Common.m_sTabBar.m_bDispTabWnd ){	/* タブバーを表示する */
		if( nullptr == m_cTabWnd.GetHwnd() ){
			m_cTabWnd.Open( G_AppInstance(), GetHwnd() );
			// タブバーが後から作成された場合、ダークモードのテーマを適用する
			if( IsDarkModeActive() ){
				DarkMode::setChildCtrlsSubclassAndTheme( m_cTabWnd.GetHwnd() );
			}
		}else{
			m_cTabWnd.UpdateStyle();
		}
	}else{
		m_cTabWnd.Close();
		m_cTabWnd.SizeBox_ONOFF(false);
	}
}

/*! ミニマップの配置処理
	@date 2014.07.14 新規作成
*/
void CEditWnd::LayoutMiniMap( void )
{
	if( m_pShareData->m_Common.m_sWindow.m_bDispMiniMap ){	/* タブバーを表示する */
		if( !m_cMiniMapView.GetHwnd() ){
			m_cMiniMapView.Create( GetHwnd() );
		}
	}else{
		if( m_cMiniMapView.GetHwnd() ){
			m_cMiniMapView.Close();
		}
	}
}

/*! バーの配置終了処理
	@date 2006.12.19 ryoji 新規作成
	@date 2007.03.04 ryoji 印刷プレビュー時はバーを隠す
	@date 2011.01.21 ryoji アウトライン画面にゴミが描画されるのを抑止する
*/
void CEditWnd::EndLayoutBars( BOOL bAdjust/* = TRUE*/ )
{
	int nCmdShow = m_pPrintPreview? SW_HIDE: SW_SHOW;
	if (const auto hwndToolBar = (nullptr != m_cToolbar.GetRebarHwnd()) ? m_cToolbar.GetRebarHwnd() : m_cToolbar.GetToolbarHwnd())
		::ShowWindow( hwndToolBar, nCmdShow );
	if( m_cStatusBar.GetStatusHwnd() )
		::ShowWindow( m_cStatusBar.GetStatusHwnd(), nCmdShow );
	if( nullptr != m_cFuncKeyWnd.GetHwnd() )
		::ShowWindow( m_cFuncKeyWnd.GetHwnd(), nCmdShow );
	if( nullptr != m_cTabWnd.GetHwnd() )
		::ShowWindow( m_cTabWnd.GetHwnd(), nCmdShow );
	if( nullptr != m_cDlgFuncList.GetHwnd() && m_cDlgFuncList.IsDocking() ){
		::ShowWindow( m_cDlgFuncList.GetHwnd(), nCmdShow );
		// アウトラインを最背後にしておく（ゴミ描画の抑止策）
		// この対策以前は、アウトラインを下ドッキングしている状態で、
		// メニューから[ファンクションキーを表示]/[ステータスバーを表示]を実行して非表示のバーをアウトライン直下に表示したり、
		// その後、ウィンドウの下部境界を上下ドラッグしてサイズ変更するとゴミが現れることがあった。
		::SetWindowPos( m_cDlgFuncList.GetHwnd(), HWND_BOTTOM, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE );
	}
	if( m_cMiniMapView.GetHwnd() ){
		::ShowWindow( m_cMiniMapView.GetHwnd(), nCmdShow );
	}

	if( bAdjust )
	{
		RECT		rc;
		m_cSplitterWnd.DoSplit( -1, -1 );
		::GetClientRect( GetHwnd(), &rc );
		auto nWinSizeType = m_nWinSizeType;
		::SendMessage( GetHwnd(), WM_SIZE, 0, 0 ); // ツールバーの表示ON/OFFを行うとちらつきが発生する事への対策
		::SendMessage( GetHwnd(), WM_SIZE, nWinSizeType, MAKELONG( rc.right - rc.left, rc.bottom - rc.top ) );
		::RedrawWindow( GetHwnd(), nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW );	// ステータスバーに必要？

		GetActiveView().SetIMECompFormPos();
	}
}

static inline BOOL MyIsDialogMessage(HWND hwnd, MSG* msg)
{
	if(hwnd==nullptr)return FALSE;
	return ::IsDialogMessage(hwnd, msg);
}

//複数プロセス版
/* メッセージループ */
//2004.02.17 Moca GetMessageのエラーチェック
void CEditWnd::MessageLoop( void )
{
	MSG	msg;
	int ret;

	auto hWndDM = GetHwnd();
	DarkMode::setDarkWndNotifySafeEx(hWndDM, false, true);

	while(GetHwnd())
	{
		//メッセージ取得
		ret = GetMessage(&msg,nullptr,0,0);
		if(ret== 0)break; //WM_QUIT
		if(ret==-1)break; //GetMessage失敗

		//ダイアログメッセージ
		     if( MyIsDialogMessage( CPrintPreview::GetPrintPreviewBarHANDLE_Safe(m_pPrintPreview.get()),	&msg ) ){}	//!< 印刷プレビュー 操作バー
		else if( MyIsDialogMessage( m_cDlgFind.GetHwnd(),								&msg ) ){}	//!<「検索」ダイアログ
		else if( MyIsDialogMessage( m_cDlgFuncList.GetHwnd(),							&msg ) ){}	//!<「アウトライン」ダイアログ
		else if( MyIsDialogMessage( m_cDlgReplace.GetHwnd(),							&msg ) ){}	//!<「置換」ダイアログ
		else if( MyIsDialogMessage( m_cDlgGrep.GetHwnd(),								&msg ) ){}	//!<「Grep」ダイアログ
		else if( MyIsDialogMessage( m_cHokanMgr.GetHwnd(),								&msg ) ){}	//!<「入力補完」
		else if( m_cToolbar.EatMessage(&msg ) ){ }													//!<ツールバー
		//アクセラレータ
		else{
			// 補完ウィンドウが表示されているときはキーボード入力を先に処理させる（カーソル移動／決定／キャンセルの処理）
			if (WM_KEYDOWN == msg.message &&
				GetActiveView().m_bHokan &&
				-1 == m_cHokanMgr.KeyProc(msg.wParam, msg.lParam)) {
						continue;	// 補完ウィンドウが処理を実行した
			}

			if( m_hAccel && TranslateAccelerator( msg.hwnd, m_hAccel, &msg ) ){}
			//通常メッセージ
			else{
				TranslateMessage( &msg );
				DispatchMessage( &msg );
			}
		}
	}
}

LRESULT CEditWnd::DispatchEvent(
	HWND	hwnd,	// handle of window
	UINT	uMsg,	// message identifier
	WPARAM	wParam,	// first message parameter
	LPARAM	lParam 	// second message parameter
)
{
	const auto hWnd = GetHwnd();

	int					nRet;
	LPNMHDR				pnmh;
	int					nPane;
	EditInfo*			pfi;

	UINT				idCtl;	/* コントロールのID */
	LPDRAWITEMSTRUCT	lpdis;	/* 項目描画情報 */
	UINT				uItem;
	LRESULT				lRes;
	CTypeConfig			cTypeNew;

	switch( uMsg ){
	case MYWM_CURL_SYNTAX_RESULT:
		AcceptCurlSyntaxResult();
		return 0L;
	case MYWM_CSV_TABLE_PARSE_RESULT:
		AcceptCSVTableParseResult();
		return 0L;
	case MYWM_CSV_TABLE_QUERY_RESULT:
		AcceptCSVTableQueryResult();
		return 0L;
	case MYWM_CSV_VALIDATION_RESULT:
		AcceptCSVValidationResult();
		return 0L;
	case MYWM_REALTIME_FILTER_RESULT:
		AcceptRealtimeFilterResult();
		return 0L;
	case WM_PAINTICON:
		return 0;
	case WM_ICONERASEBKGND:
		return 0;
	case WM_LBUTTONDOWN:
		return OnLButtonDown( wParam, lParam );
	case WM_MOUSEMOVE:
		return OnMouseMove( wParam, lParam );
	case WM_LBUTTONUP:
		return OnLButtonUp( wParam, lParam );
	case WM_MOUSEWHEEL:
		return OnMouseWheel( wParam, lParam );
	case WM_HSCROLL:
		return OnHScroll( wParam, lParam );
	case WM_VSCROLL:
		return OnVScroll( wParam, lParam );

	case WM_MENUCHAR:
		/* メニューアクセスキー押下時の処理(WM_MENUCHAR処理) */
		return m_cMenuDrawer.OnMenuChar( hwnd, uMsg, wParam, lParam );

	// 2007.09.09 Moca 互換BMPによる画面バッファ
	case WM_SHOWWINDOW:
		if( !wParam ){
			Views_DeleteCompatibleBitmap();
		}
		return ::DefWindowProc( hwnd, uMsg, wParam, lParam );

	case WM_MENUSELECT:
		if( nullptr == m_cStatusBar.GetStatusHwnd() ){
			return 1;
		}
		uItem = (UINT) LOWORD(wParam);		// menu item or submenu index
		{
			/* メニュー機能のテキストをセット */
			CNativeW	cmemWork;

			/* 機能に対応するキー名の取得(複数) */
			CNativeW**	ppcAssignedKeyList;
			int			nAssignedKeyNum;
			int			j;
			nAssignedKeyNum = CKeyBind::GetKeyStrList(
				G_AppInstance(),
				m_pShareData->m_Common.m_sKeyBind.m_nKeyNameArrNum,
				(KEYDATA*)m_pShareData->m_Common.m_sKeyBind.m_pKeyNameArr,
				&ppcAssignedKeyList,
				uItem
			);
			if( 0 < nAssignedKeyNum ){
				for( j = 0; j < nAssignedKeyNum; ++j ){
					if( j > 0 ){
						cmemWork.AppendString(L" , ");
					}
					cmemWork.AppendNativeData( *ppcAssignedKeyList[j] );
					delete ppcAssignedKeyList[j];
				}
				delete [] ppcAssignedKeyList;
			}

			const WCHAR* pszItemStr = cmemWork.GetStringPtr();

			m_cStatusBar.SetStatusText(0, SBT_NOBORDERS, pszItemStr);
		}
		return 0;

	case WM_DRAWITEM:
		idCtl = (UINT) wParam;				/* コントロールのID */
		lpdis = (DRAWITEMSTRUCT*) lParam;	/* 項目描画情報 */
		if( IDW_STATUSBAR == idCtl ){
			if( 5 == lpdis->itemID ){ // 2003.08.26 Moca idがずれて作画されなかった
				int	nColor;
				if( m_pShareData->m_sFlags.m_bRecordingKeyMacro	/* キーボードマクロの記録中 */
				 && m_pShareData->m_sFlags.m_hwndRecordingKeyMacro == GetHwnd()	/* キーボードマクロを記録中のウィンドウ */
				){
					nColor = COLOR_BTNTEXT;
				}else{
					nColor = COLOR_3DSHADOW;
				}
				::SetTextColor( lpdis->hDC, ::GetSysColor( nColor ) );
				::SetBkMode( lpdis->hDC, TRANSPARENT );

				// 2003.08.26 Moca 上下中央位置に作画
				TEXTMETRIC tm;
				::GetTextMetrics( lpdis->hDC, &tm );
				int y = ( lpdis->rcItem.bottom - lpdis->rcItem.top - tm.tmHeight + 1 ) / 2 + lpdis->rcItem.top;
				::TextOutW(lpdis->hDC, lpdis->rcItem.left, y, PSZ_ARGS(L"REC"));
				if( COLOR_BTNTEXT == nColor ){
					::TextOutW(lpdis->hDC, lpdis->rcItem.left + 1, y, PSZ_ARGS(L"REC"));
				}
			}
			return 0;
		}
		return FALSE;
	case WM_PAINT:
		return OnPaint( hwnd, uMsg, wParam, lParam );

	case WM_PASTE:
		return GetActiveView().GetCommander().HandleCommand( F_PASTE, true, 0, 0, 0, 0 );

	case WM_COPY:
		return GetActiveView().GetCommander().HandleCommand( F_COPY, true, 0, 0, 0, 0 );

	case WM_HELP:
		if (const auto lphi = (LPHELPINFO) lParam; lphi && HELPINFO_MENUITEM == lphi->iContextType) {
			MyWinHelp( hwnd, HELP_CONTEXT, FuncID_To_HelpContextID( (EFunctionCode)lphi->iCtrlId ) );
		}
		return TRUE;

	case WM_ACTIVATEAPP:
		m_bIsActiveApp = (wParam != 0);	// 自アプリがアクティブかどうか

		// アクティブ化なら編集ウィンドウリストの先頭に移動する		// 2007.04.08 ryoji WM_SETFOCUS から移動
		if( m_bIsActiveApp ){
			CAppNodeGroupHandle(0).AddEditWndList( GetHwnd() );	// リスト移動処理

			// 2009.01.17 nasukoji	ホイールスクロール有無状態をクリア
			ClearMouseState();
		}

		// キャプション設定、タイマーON/OFF		// 2007.03.08 ryoji WM_ACTIVATEから移動
		UpdateCaption();
		m_cFuncKeyWnd.Timer_ONOFF( m_bIsActiveApp ); // 20060126 aroka
		this->Timer_ONOFF( m_bIsActiveApp ); // 20060128 aroka

		return 0L;

	case WM_ENABLE:
		// 右ドロップファイルの受け入れ設定／解除	// 2009.01.09 ryoji
		// Note: DragAcceptFilesを適用した左ドロップについては Enable/Disable で自動的に受け入れ設定／解除が切り替わる
		if( (BOOL)wParam ){
			m_pcDropTarget->Register_DropTarget( m_hWnd );
		}else{
			m_pcDropTarget->Revoke_DropTarget();
		}
		return 0L;

	case WM_WINDOWPOSCHANGED:
		// ポップアップウィンドウの表示切替指示をポストする	// 2007.10.22 ryoji
		// ・WM_SHOWWINDOWはすべての表示切替で呼ばれるわけではないのでWM_WINDOWPOSCHANGEDで処理
		//   （タブグループ解除などの設定変更時はWM_SHOWWINDOWは呼ばれない）
		// ・即時切替だとタブ切替に干渉して元のタブに戻ってしまうことがあるので後で切り替える
		if (const auto pwp = (WINDOWPOS*)lParam;
			pwp->flags & SWP_SHOWWINDOW)
			::PostMessage( hwnd, MYWM_SHOWOWNEDPOPUPS, TRUE, 0 );
		else if( pwp->flags & SWP_HIDEWINDOW )
			::PostMessage( hwnd, MYWM_SHOWOWNEDPOPUPS, FALSE, 0 );

		return ::DefWindowProc( hwnd, uMsg, wParam, lParam );

	case MYWM_SHOWOWNEDPOPUPS:
		::ShowOwnedPopups( m_hWnd, (BOOL)wParam );	// 2007.10.22 ryoji
		return 0L;

	case WM_SIZE:
//		MYTRACE( L"WM_SIZE\n" );
		/* WM_SIZE 処理 */
		if( SIZE_MINIMIZED == wParam ){
			this->UpdateCaption();
		}
		return OnSize( wParam, lParam );

	//From here 2003.05.31 MIK
	case WM_MOVE:
		// From Here 2004.05.13 Moca ウィンドウ位置継承
		//	最後の位置を復元するため，移動されるたびに共有メモリに位置を保存する．
		if (WINSIZEMODE_SAVE == m_pShareData->m_Common.m_sWindow.m_eSaveWindowPos &&
			!::IsZoomed(hWnd) &&
			!::IsIconic(hWnd)) {
				// 2005.11.23 Moca ワークエリア座標だとずれるのでスクリーン座標に変更
				// Aero Snapで縦方向最大化で終了して次回起動するときは元のサイズにする必要があるので、
				// GetWindowRect()ではなくGetWindowPlacement()で得たワークエリア座標をスクリーン座標に変換して記憶する	// 2009.09.02 ryoji
				RECT rcWin;
				WINDOWPLACEMENT wp;
				wp.length = sizeof(wp);
				::GetWindowPlacement( GetHwnd(), &wp );	// ワークエリア座標
				rcWin = wp.rcNormalPosition;
				RECT rcWork, rcMon;
				GetMonitorWorkRect( GetHwnd(), &rcWork, &rcMon );
				::OffsetRect(&rcWin, rcWork.left - rcMon.left, rcWork.top - rcMon.top);	// スクリーン座標に変換
				m_pShareData->m_Common.m_sWindow.m_nWinPosX = rcWin.left;
				m_pShareData->m_Common.m_sWindow.m_nWinPosY = rcWin.top;
		}
		// To Here 2004.05.13 Moca ウィンドウ位置継承
		return DefWindowProc( hwnd, uMsg, wParam, lParam );
	//To here 2003.05.31 MIK
	case WM_SYSCOMMAND:
		// タブまとめ表示では閉じる動作はオプション指定に従う	// 2006.02.13 ryoji
		//	Feb. 11, 2007 genta 動作を選べるように(MDI風と従来動作)
		// 2007.02.22 ryoji Alt+F4 のデフォルト機能でモード毎の動作が得られるようになった
		if( wParam == SC_CLOSE ){
			// 印刷プレビューモードでウィンドウを閉じる操作のときはプレビューを閉じる	// 2007.03.04 ryoji
			if( m_pPrintPreview ){
				PrintPreviewModeONOFF();	// 印刷プレビューモードのオン/オフ
				return 0L;
			}
			OnCommand( 0, (WORD)CKeyBind::GetDefFuncCode( VK_F4, _ALT ), nullptr );
			return 0L;
		}
		return DefWindowProc( hwnd, uMsg, wParam, lParam );
#if 0
	case WM_IME_COMPOSITION:
		if ( lParam & GCS_RESULTSTR ) {
			/* メッセージの配送 */
			return Views_DispatchEvent( hwnd, uMsg, wParam, lParam );
		}else{
			return DefWindowProc( hwnd, uMsg, wParam, lParam );
		}
#endif
	//case WM_KILLFOCUS:
	case WM_CHAR:
	case WM_IME_CHAR:
	case WM_KEYUP:
	case WM_SYSKEYUP:	// 2004.04.28 Moca ALT+キーのキーリピート処理のため追加
	case WM_ENTERMENULOOP:
#if 0
	case MYWM_IME_REQUEST:   /*  再変換対応 by minfu 2002.03.27  */ // 20020331 aroka
#endif
		if( GetActiveView().m_nAutoScrollMode ){
			GetActiveView().AutoScrollExit();
		}
		/* メッセージの配送 */
		return Views_DispatchEvent( hwnd, uMsg, wParam, lParam );

	case WM_EXITMENULOOP:
//		MYTRACE( L"WM_EXITMENULOOP\n" );
		if( nullptr != m_cStatusBar.GetStatusHwnd() ){
			m_cStatusBar.SetStatusText(0, SBT_NOBORDERS, L"");
		}
		/* メッセージの配送 */
		return Views_DispatchEvent( hwnd, uMsg, wParam, lParam );

	case WM_SETFOCUS:
//		MYTRACE( L"WM_SETFOCUS\n" );

		// Aug. 29, 2003 wmlhq & ryojiファイルのタイムスタンプのチェック処理 OnTimer に移行
		m_nTimerCount = 9;

		// ビューにフォーカスを移動する	// 2007.10.16 ryoji
		if( !m_pPrintPreview ){
			::SetFocus(m_bRealtimeFilterMode && m_hwndRealtimeFilterList ? m_hwndRealtimeFilterList :
				m_bCSVTableMode && m_hwndCSVTable ? m_hwndCSVTable : GetActiveView().GetHwnd());
		}
		lRes = 0;

//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
		/* 印刷プレビューモードのときは、キー操作は全部PrintPreviewBarへ転送 */
		if( m_pPrintPreview ){
			m_pPrintPreview->SetFocusToPrintPreviewBar();
		}

		return lRes;

	case WM_NOTIFY:
		pnmh = (LPNMHDR) lParam;
		if (!pnmh) return 0L;
		if (m_bRealtimeFilterMode && m_hwndRealtimeFilterList &&
			pnmh->hwndFrom == m_hwndRealtimeFilterList && pnmh->code == LVN_GETDISPINFOW) {
			auto* display = reinterpret_cast<NMLVDISPINFOW*>(lParam);
			if ((display->item.mask & LVIF_TEXT) != 0 && display->item.iItem >= 0 &&
				GetDocument() && GetDocument()->m_uCurlSyntaxVersion == m_uRealtimeFilterDocumentVersion) {
				const auto sourceLine = textfilter::SourceLineForVisibleIndex(
					m_vRealtimeFilterVisibleRanges, static_cast<std::size_t>(display->item.iItem));
				if (sourceLine && *sourceLine < m_vRealtimeFilterLineIndex.size()) {
					const CDocLine* line = m_vRealtimeFilterLineIndex[*sourceLine];
					if (line) {
						try {
							const std::size_t lineLength = static_cast<std::size_t>(line->GetLengthWithoutEOL());
							m_strRealtimeFilterDisplayText = std::to_wstring(*sourceLine + 1) + L": ";
							m_strRealtimeFilterDisplayText.append(line->GetPtr(), lineLength);
							display->item.pszText = m_strRealtimeFilterDisplayText.data();
						} catch (...) {
							m_strRealtimeFilterDisplayText.clear();
							display->item.pszText = m_strRealtimeFilterDisplayText.data();
						}
					}
				}
			}
			return 0;
		}
		if (m_hwndCSVTable && pnmh->hwndFrom == m_hwndCSVTable && pnmh->code == NM_RCLICK) {
			const auto* activate = reinterpret_cast<const NMITEMACTIVATE*>(lParam);
			if (activate->iSubItem > 0) m_nCSVCurrentColumn = activate->iSubItem;
			POINT screenPoint{};
			::GetCursorPos(&screenPoint);
			ShowCSVTableContextMenu(screenPoint, activate->iItem, activate->iSubItem);
			return 0;
		}
		if (m_hwndCSVTable && pnmh->hwndFrom == m_hwndCSVTable && pnmh->code == LVN_GETDISPINFOW && m_csvTableModel) {
			auto* display = reinterpret_cast<NMLVDISPINFOW*>(lParam);
			if ((display->item.mask & LVIF_TEXT) != 0 && display->item.iItem >= 0) {
				try {
					if (display->item.iSubItem == 0) {
						m_csvTableDisplayText = std::to_wstring(static_cast<std::size_t>(display->item.iItem) + 1);
					} else {
						const std::size_t sourceRow = CSVSourceRow(static_cast<std::size_t>(display->item.iItem));
						m_csvTableDisplayText = sourceRow < m_csvTableModel->RowCount()
							? m_csvTableModel->CellText(sourceRow, static_cast<std::size_t>(display->item.iSubItem - 1))
							: std::wstring{};
						std::replace(m_csvTableDisplayText.begin(), m_csvTableDisplayText.end(), L'\r', L' ');
						std::replace(m_csvTableDisplayText.begin(), m_csvTableDisplayText.end(), L'\n', L' ');
					}
				} catch (...) {
					m_csvTableDisplayText.clear();
				}
				display->item.pszText = m_csvTableDisplayText.data();
			}
			return 0;
		}
		if (m_hwndCSVTable && pnmh->hwndFrom == m_hwndCSVTable && pnmh->code == NM_CLICK) {
			const auto* click = reinterpret_cast<const NMITEMACTIVATE*>(lParam);
			if (click->iSubItem > 0) m_nCSVCurrentColumn = click->iSubItem;
			::SetTimer(GetHwnd(), IDT_CSV_SELECTION_STATS, 150, nullptr);
			return 0;
		}
		if (m_hwndCSVTable && pnmh->hwndFrom == m_hwndCSVTable && pnmh->code == LVN_ITEMCHANGED) {
			const auto* change = reinterpret_cast<const NMLISTVIEW*>(lParam);
			if ((change->uChanged & LVIF_STATE) != 0 &&
				((change->uOldState ^ change->uNewState) & LVIS_SELECTED) != 0) {
				::SetTimer(GetHwnd(), IDT_CSV_SELECTION_STATS, 150, nullptr);
			}
			return 0;
		}
		if (m_hwndCSVTable && pnmh->hwndFrom == m_hwndCSVTable && pnmh->code == NM_DBLCLK) {
			const auto* activate = reinterpret_cast<const NMITEMACTIVATE*>(lParam);
			if (activate->iItem >= 0 && activate->iSubItem > 0) {
				m_nCSVCurrentColumn = activate->iSubItem;
				BeginCSVCellEdit(activate->iItem, activate->iSubItem);
			}
			return 0;
		}
		if (m_hwndCSVTable && pnmh->hwndFrom == m_hwndCSVTable && pnmh->code == LVN_KEYDOWN) {
			const auto* key = reinterpret_cast<const NMLVKEYDOWN*>(lParam);
			if (key->wVKey == VK_F2) {
				const int row = ListView_GetNextItem(m_hwndCSVTable, -1, LVNI_FOCUSED);
				if (row >= 0) BeginCSVCellEdit(row, m_nCSVCurrentColumn);
				return 0;
			}
		}
		if (m_hwndCSVTable && pnmh->hwndFrom == ListView_GetHeader(m_hwndCSVTable) &&
			pnmh->code == NM_RCLICK) {
			POINT point{};
			::GetCursorPos(&point);
			HWND header = ListView_GetHeader(m_hwndCSVTable);
			POINT clientPoint = point;
			::ScreenToClient(header, &clientPoint);
			HDHITTESTINFO hit{};
			hit.pt = clientPoint;
			::SendMessageW(header, HDM_HITTEST, 0, reinterpret_cast<LPARAM>(&hit));
			if (hit.iItem > 0) {
				ShowCSVFilterMenu(static_cast<std::size_t>(hit.iItem - 1), point);
			}
			return 0;
		}
		if (m_hwndCSVTable && pnmh->hwndFrom == ListView_GetHeader(m_hwndCSVTable) &&
			pnmh->code == HDN_ENDTRACKW) {
			const auto* headerResize = reinterpret_cast<const NMHEADERW*>(lParam);
			if (headerResize->iItem > 0 && headerResize->iItem <= 512 && headerResize->pitem) {
				auto& widths = csvtable::GetUserSettings().columnWidths;
				const std::size_t column = static_cast<std::size_t>(headerResize->iItem - 1);
				if (widths.size() <= column) widths.resize(column + 1, 0);
				widths[column] = static_cast<int>(std::clamp<LONG>(DpiUnscaleX(headerResize->pitem->cxy), 24L, 4000L));
			}
			return 0;
		}
		if (m_hwndCSVTable && pnmh->hwndFrom == ListView_GetHeader(m_hwndCSVTable) &&
			(pnmh->code == HDN_ITEMCLICKW || pnmh->code == HDN_ITEMCLICK)) {
			const auto* headerClick = reinterpret_cast<const NMHEADERW*>(lParam);
			if (headerClick->iItem > 0) {
				const int focusedRow = ListView_GetNextItem(m_hwndCSVTable, -1, LVNI_FOCUSED);
				m_nCSVSortPendingSourceRow = focusedRow >= 0
					? static_cast<int>(CSVSourceRow(static_cast<std::size_t>(focusedRow)))
					: -1;
				const int column = headerClick->iItem - 1;
				m_nCSVCurrentColumn = column + 1;
				::SetTimer(GetHwnd(), IDT_CSV_SELECTION_STATS, 150, nullptr);
				const bool addSortKey = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
			auto key = std::find_if(m_vCSVSortKeys.begin(), m_vCSVSortKeys.end(),
				[column](const csvtable::SortKey& item) { return item.column == static_cast<std::size_t>(column); });
			if (!addSortKey) {
				const bool toggleDescending = m_vCSVSortKeys.size() == 1 && key != m_vCSVSortKeys.end();
				const csvtable::SortKind kind = key != m_vCSVSortKeys.end()
					? key->kind : csvtable::SortKind::String;
				const bool descending = toggleDescending && !key->descending;
				m_vCSVSortKeys.assign(1, { static_cast<std::size_t>(column), kind, descending });
			} else if (key == m_vCSVSortKeys.end()) {
				m_vCSVSortKeys.push_back({ static_cast<std::size_t>(column), csvtable::SortKind::String, false });
			} else {
				key->descending = !key->descending;
			}
				m_nCSVPendingEditRow = m_nCSVPendingEditColumn = -1;
				UpdateCSVSortIndicator();
				ScheduleCSVTableQuery();
			}
			return 0;
		}
		if (m_hwndCurlSyntaxErrorList && pnmh->hwndFrom == m_hwndCurlSyntaxErrorList && pnmh->code == NM_DBLCLK) {
			const int selected = ListView_GetNextItem(m_hwndCurlSyntaxErrorList, -1, LVNI_SELECTED);
			if (selected >= 0) {
				LVITEMW item{};
				item.mask = LVIF_PARAM;
				item.iItem = selected;
				if (ListView_GetItem(m_hwndCurlSyntaxErrorList, &item)) {
					if (m_bCSVValidationListActive) GoToCSVValidationDiagnostic(static_cast<std::size_t>(item.lParam));
					else GoToCurlSyntaxDiagnostic(static_cast<size_t>(item.lParam));
				}
			}
			return 0;
		}
		//	From Here Feb. 15, 2004 genta
		//	ステータスバーのダブルクリックでモード切替ができるようにする
		if( m_cStatusBar.GetStatusHwnd() && pnmh->hwndFrom == m_cStatusBar.GetStatusHwnd() ){
			if( pnmh->code == NM_DBLCLK ){
				LPNMMOUSE mp = (LPNMMOUSE) lParam;
				if( mp->dwItemSpec == 6 ){	//	上書き/挿入
					GetDocument()->HandleCommand( F_CHGMOD_INS );
				}
				else if( mp->dwItemSpec == 5 ){	//	マクロの記録開始・終了
					GetDocument()->HandleCommand( F_RECKEYMACRO );
				}
				else if( mp->dwItemSpec == 1 ){	//	桁位置→行番号ジャンプ
					GetDocument()->HandleCommand( F_JUMP_DIALOG );
				}
				else if( mp->dwItemSpec == 3 ){	//	文字コード→各種コード
					ShowCodeBox( GetHwnd(), GetDocument() );
				}
				else if( mp->dwItemSpec == 4 ){	//	文字コードセット→文字コードセット指定
					GetDocument()->HandleCommand( F_CHG_CHARSET );
				}
			}
			else if( pnmh->code == NM_RCLICK ){
				LPNMMOUSE mp = (LPNMMOUSE) lParam;
				if( mp->dwItemSpec == 2 ){	//	入力改行モード
					m_cMenuDrawer.ResetContents();
					HMENU hMenuPopUp = ::CreatePopupMenu();
					m_cMenuDrawer.MyAppendMenu( hMenuPopUp, MF_BYPOSITION | MF_STRING, F_CHGMOD_EOL_CRLF,
						LS( F_CHGMOD_EOL_CRLF ), L"C" ); // 入力改行コード指定(CRLF)
					m_cMenuDrawer.MyAppendMenu( hMenuPopUp, MF_BYPOSITION | MF_STRING, F_CHGMOD_EOL_LF,
						LS( F_CHGMOD_EOL_LF ), L"L" ); // 入力改行コード指定(LF)
					m_cMenuDrawer.MyAppendMenu( hMenuPopUp, MF_BYPOSITION | MF_STRING, F_CHGMOD_EOL_CR,
						LS( F_CHGMOD_EOL_CR ), L"R" ); // 入力改行コード指定(CR)
					// 拡張EOLが有効の時だけ表示
					if( GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol ){
						m_cMenuDrawer.MyAppendMenu( hMenuPopUp, MF_BYPOSITION | MF_STRING, F_CHGMOD_EOL_NEL,
							LS(STR_EDITWND_MENU_NEL), L"", TRUE, -2 ); // 入力改行コード指定(NEL)
						m_cMenuDrawer.MyAppendMenu( hMenuPopUp, MF_BYPOSITION | MF_STRING, F_CHGMOD_EOL_LS,
							LS(STR_EDITWND_MENU_LS), L"", TRUE, -2 ); // 入力改行コード指定(LS)
						m_cMenuDrawer.MyAppendMenu( hMenuPopUp, MF_BYPOSITION | MF_STRING, F_CHGMOD_EOL_PS,
							LS(STR_EDITWND_MENU_PS), L"", TRUE, -2 ); // 入力改行コード指定(PS)
					}

					//	mp->ptはステータスバー内部の座標なので，スクリーン座標への変換が必要
					POINT	po = mp->pt;
					::ClientToScreen( m_cStatusBar.GetStatusHwnd(), &po );
					EFunctionCode nId = (EFunctionCode)::TrackPopupMenu(
						hMenuPopUp,
						TPM_CENTERALIGN
						| TPM_BOTTOMALIGN
						| TPM_RETURNCMD
						| TPM_LEFTBUTTON
						,
						po.x,
						po.y,
						0,
						GetHwnd(),
						nullptr
					);
					::DestroyMenu( hMenuPopUp );
					EEolType nEOLCode;
					switch(nId){
					case F_CHGMOD_EOL_CRLF:	nEOLCode = EEolType::cr_and_lf; break;
					case F_CHGMOD_EOL_CR:	nEOLCode = EEolType::carriage_return; break;
					case F_CHGMOD_EOL_LF:	nEOLCode = EEolType::line_feed; break;
					case F_CHGMOD_EOL_NEL:	nEOLCode = EEolType::next_line; break;
					case F_CHGMOD_EOL_PS:	nEOLCode = EEolType::paragraph_separator; break;
					case F_CHGMOD_EOL_LS:	nEOLCode = EEolType::line_separator; break;
					default:
						nEOLCode = EEolType::none;
					}
					if( !CEol::IsNone( nEOLCode ) ){
						GetActiveView().GetCommander().HandleCommand( F_CHGMOD_EOL, true, static_cast<LPARAM>(nEOLCode), 0, 0, 0 );
					}
				}
			}
			return 0L;
		}
		//	To Here Feb. 15, 2004 genta

		switch( pnmh->code ){
		// 2007.09.08 kobake TTN_NEEDTEXTの処理をA版とW版に分けて明示的に処理するようにしました。
		//                   ※テキストが80文字を超えそうならTOOLTIPTEXT::lpszTextを利用してください。
		// 2008.11.03 syat   矩形範囲選択開始のツールチップで80文字超えていたのでlpszTextに変更。
		case TTN_NEEDTEXT:
			{
				static WCHAR szText[256];
				memset(szText, 0, sizeof(szText));

				//ツールチップテキスト取得、設定
				LPTOOLTIPTEXT lptip = (LPTOOLTIPTEXT)pnmh;
				if (!m_cToolbar.GetEnhancementTooltipText(lptip->hdr.hwndFrom, lptip->hdr.idFrom,
					szText, std::size(szText))) {
					GetTooltipText(szText, int(std::size(szText)), lptip->hdr.idFrom);
				}
				lptip->lpszText = szText;
			}
			break;

		case TBN_DROPDOWN:
			{
				int	nId;
				nId = CreateFileDropDownMenu( pnmh->hwndFrom );
				if( nId != 0 ) OnCommand( (WORD)0 /*メニュー*/, (WORD)nId, nullptr );
			}
			return FALSE;
		default:
			break;
		}
		return 0L;
	case WM_COMMAND:
		OnCommand( HIWORD(wParam), LOWORD(wParam), (HWND) lParam );
		return 0L;
	case WM_INITMENUPOPUP:
		InitMenu( (HMENU)wParam, (UINT)LOWORD( lParam ), (BOOL)HIWORD( lParam ) );
		return 0L;
	case WM_DROPFILES:
		/* ファイルがドロップされた */
		OnDropFiles( (HDROP) wParam );
		return 0L;
	case WM_QUERYENDSESSION:	//OSの終了
		if( OnClose( nullptr, false ) ){
			::DestroyWindow( hwnd );
			return TRUE;
		}
		else{
			return FALSE;
		}
	case WM_CLOSE:
		if( OnClose( nullptr, false ) ){
			::DestroyWindow( hwnd );
		}
		return 0L;
	case WM_DESTROY:
		::KillTimer(GetHwnd(), IDT_CURL_SYNTAX);
		::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_DEBOUNCE);
		::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN);
		if (m_pRealtimeFilterService) m_pRealtimeFilterService->Cancel();
		if (m_pCurlSyntaxCheckService) m_pCurlSyntaxCheckService->Cancel();
		if (m_pCSVTableParseService) m_pCSVTableParseService->Cancel();
		if (m_pCSVTableQueryService) m_pCSVTableQueryService->Cancel();
		if (m_pCSVValidationService) m_pCSVValidationService->Cancel();
		if( m_pShareData->m_sFlags.m_bRecordingKeyMacro ){					/* キーボードマクロの記録中 */
			if( m_pShareData->m_sFlags.m_hwndRecordingKeyMacro == GetHwnd() ){	/* キーボードマクロを記録中のウィンドウ */
				m_pShareData->m_sFlags.m_bRecordingKeyMacro = FALSE;			/* キーボードマクロの記録中 */
				m_pShareData->m_sFlags.m_hwndRecordingKeyMacro = nullptr;		/* キーボードマクロを記録中のウィンドウ */
			}
		}

		/* タイマーを削除 */
		::KillTimer( GetHwnd(), IDT_TOOLBAR );

		/* ドロップされたファイルを受け入れるのを解除 */
		::DragAcceptFiles( hwnd, FALSE );
		m_pcDropTarget->Revoke_DropTarget();	// 右ボタンドロップ用	// 2008.06.20 ryoji

		/* 編集ウィンドウリストからの削除 */
		CAppNodeGroupHandle(GetHwnd()).DeleteEditWndList( GetHwnd() );

		if( m_pShareData->m_sHandles.m_hwndDebug == GetHwnd() ){
			m_pShareData->m_sHandles.m_hwndDebug = nullptr;
		}
		m_hWnd = nullptr;

		/* 編集ウィンドウオブジェクトからのオブジェクト削除要求 */
		::PostMessageAny( m_pShareData->m_sHandles.m_hwndTray, MYWM_DELETE_ME, 0, 0 );

		/* Windows にスレッドの終了を要求します */
		::PostQuitMessage( 0 );

		return 0L;

	case WM_THEMECHANGED:
		// 2006.06.17 ryoji
		// ビジュアルスタイル／クラシックスタイルが切り替わったらツールバーを再作成する
		// （ビジュアルスタイル: Rebar 有り、クラシックスタイル: Rebar 無し）
		if( m_cToolbar.GetToolbarHwnd() ){
			if( IsVisualStyle() == (nullptr == m_cToolbar.GetRebarHwnd()) ){
				m_cToolbar.DestroyToolBar();
				LayoutToolBar();
				EndLayoutBars();
			}
		}
		return 0L;

	case MYWM_UIPI_CHECK:
		/* エディタ－トレイ間でのUI特権分離の確認メッセージ */	// 2007.06.07 ryoji
		return LRESULT(lParam);

	case MYWM_CLOSE:
		/* エディタへの終了要求 */
		if( FALSE != ( nRet = OnClose( (HWND)lParam,
				PM_CLOSE_GREPNOCONFIRM == (PM_CLOSE_GREPNOCONFIRM & wParam) )) ){	// Jan. 23, 2002 genta 警告抑制
			//プラグイン：DocumentCloseイベント実行
			CJackManager::getInstance()->InvokePlugins( PP_DOCUMENT_CLOSE, &GetActiveView() );

			//プラグイン：EditorEndイベント実行
			CJackManager::getInstance()->InvokePlugins( PP_EDITOR_END, &GetActiveView() );

			// タブまとめ表示では閉じる動作はオプション指定に従う	// 2006.02.13 ryoji
			if (PM_CLOSE_EXIT != (PM_CLOSE_EXIT & wParam) &&	// 全終了要求でない場合
				// タブまとめ表示で(無題)を残す指定の場合、残ウィンドウが１個なら新規エディタを起動して終了する
				m_pShareData->m_Common.m_sTabBar.m_bDispTabWnd &&
				!m_pShareData->m_Common.m_sTabBar.m_bDispTabWndMultiWin &&
				m_pShareData->m_Common.m_sTabBar.m_bTab_RetainEmptyWin
					){
					// 自グループ内の残ウィンドウ数を調べる	// 2007.06.20 ryoji
					int nGroup = CAppNodeManager::getInstance()->GetEditNode( GetHwnd() )->GetGroup();
					if( 1 == CAppNodeGroupHandle(nGroup).GetEditorWindowsNum() ){
						EditNode* pEditNode = CAppNodeManager::getInstance()->GetEditNode( GetHwnd() );
						if( pEditNode )
							pEditNode->m_bClosing = TRUE;	// 自分はタブ表示してもらわなくていい
						SLoadInfo sLoadInfo;
						sLoadInfo.cFilePath = L"";
						sLoadInfo.eCharCode = CODE_NONE;
						sLoadInfo.bViewMode = false;
						CControlTray::OpenNewEditor(
							G_AppInstance(),
							GetHwnd(),
							sLoadInfo,
							nullptr,
							true
						);
					}
			}
			::DestroyWindow( hwnd );
		}
		return nRet;
	case MYWM_ALLOWACTIVATE:
		::AllowSetForegroundWindow((DWORD)wParam);
		return 0L;

	case MYWM_GETFILEINFO:
		/* トレイからエディタへの編集ファイル名要求通知 */
		pfi = (EditInfo*)&m_pShareData->m_sWorkBuffer.m_EditInfo_MYWM_GETFILEINFO;

		/* 編集ファイル情報を格納 */
		GetDocument()->GetEditInfo( pfi );
		return 0L;
	case MYWM_CHANGESETTING:
		/* 設定変更の通知 */
		switch( (e_PM_CHANGESETTING_SELECT)lParam ){
		case PM_CHANGESETTING_ALL:
			/* ダークモード設定を反映する */
			{
				if( (m_pShareData->m_Common.m_sWindow.m_bDarkMode != FALSE) != IsDarkModeActive() ){
					ApplyDarkModeSetting(m_pShareData->m_Common.m_sWindow.m_bDarkMode);
					// タイトルバーとメニューバーを更新する
					DarkMode::setDarkTitleBarEx(GetHwnd(), true);
					DarkMode::setWindowMenuBarSubclass(GetHwnd());

					// 子コントロールのテーマを更新する
					DarkMode::setChildCtrlsTheme(GetHwnd());

					// タブバーのテーマを更新する（ツールチップ等）
					if( m_cTabWnd.GetHwnd() ){
						m_cTabWnd.UpdateTheme();
					}

					// エディットビューのWS_EX_STATICEDGEを切り替える
					for( int v = 0; v < GetAllViewCount(); v++ ){
						HWND hwndView = GetView(v).GetHwnd();
						DarkMode::setWindowExStyle(hwndView, !IsDarkModeActive(), WS_EX_STATICEDGE);
						::SetWindowPos(hwndView, nullptr, 0, 0, 0, 0,
							SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
					}

					// ステータスバーを再作成する（サブクラスを更新するため）
					m_cStatusBar.DestroyStatusBar();
				}
			}

			/* 言語を選択する */
			CSelectLang::ChangeLang( GetDllShareData().m_Common.m_sWindow.m_szLanguageDll );
			CShareData::getInstance()->RefreshString();

			// 2015.08.20 プリントプレビューのとき設定を延期する(戻るとき適用)
			if (!m_pPrintPreview) {
				// メインメニュー	2010/5/16 Uchi
				LayoutMainMenu();
			}

			// Oct 10, 2000 ao
			/* 設定変更時、ツールバーを再作成するようにする（バーの内容変更も反映） */
			m_cToolbar.DestroyToolBar();
			LayoutToolBar();
			// Oct 10, 2000 ao ここまで

			// 2008.10.05 nasukoji	非アクティブなウィンドウのツールバーを更新する
			// アクティブなウィンドウはタイマにより更新されるが、それ以外のウィンドウは
			// タイマを停止させており設定変更すると全部有効となってしまうため、ここで
			// ツールバーを更新する
			if( !m_bIsActiveApp )
				m_cToolbar.UpdateToolbar();

			// ファンクションキーを再作成する（バーの内容、位置、グループボタン数の変更も反映）	// 2006.12.19 ryoji
			m_cFuncKeyWnd.Close();
			LayoutFuncKey();

			// タブバーの表示／非表示切り替え	// 2006.12.19 ryoji
			LayoutTabBar();

			// ステータスバーの表示／非表示切り替え	// 2006.12.19 ryoji
			LayoutStatusBar();

			// 水平スクロールバーの表示／非表示切り替え	// 2006.12.19 ryoji
			{
				int i;
				bool b1;
				bool b2;
				b1 = (m_pShareData->m_Common.m_sWindow.m_bScrollBarHorz == FALSE);
				for( i = 0; i < GetAllViewCount(); i++ )
				{
					b2 = (GetView(i).m_hwndHScrollBar == nullptr);
					if( b1 != b2 )		/* 水平スクロールバーを使う */
					{
						GetView(i).DestroyScrollBar();
						GetView(i).CreateScrollBar();
					}
				}
			}

			LayoutMiniMap();

			// バー変更で画面が乱れないように	// 2006.12.19 ryoji
			EndLayoutBars();

			// アクセラレータテーブルを再作成する
			// ウィンドウ毎に作成したアクセラレータテーブルを破棄する
			DeleteAccelTbl();
			// ウィンドウ毎にアクセラレータテーブルを作成する
			CreateAccelTbl();

			if( m_pShareData->m_Common.m_sTabBar.m_bDispTabWnd )
			{
				// タブ表示のままグループ化する／しないが変更されていたらタブを更新する必要がある
				m_cTabWnd.Refresh( FALSE );
			}
			if( m_pShareData->m_Common.m_sTabBar.m_bDispTabWnd && !m_pShareData->m_Common.m_sTabBar.m_bDispTabWndMultiWin )
			{
				if( CAppNodeManager::getInstance()->GetEditNode( GetHwnd() )->IsTopInGroup() )
				{
					if( !::IsWindowVisible( GetHwnd() ) )
					{
						// ::ShowWindow( GetHwnd(), SW_SHOWNA ) だと非表示から表示に切り替わるときに Z-order がおかしくなることがあるので ::SetWindowPos を使う
						::SetWindowPos( GetHwnd(), nullptr,0,0,0,0,
										SWP_SHOWWINDOW | SWP_NOACTIVATE
										| SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER );

						// このウィンドウの WS_EX_TOPMOST 状態を全ウィンドウに反映する	// 2007.05.18 ryoji
						WindowTopMost( ((DWORD)::GetWindowLongPtr( GetHwnd(), GWL_EXSTYLE ) & WS_EX_TOPMOST)? 1: 2 );
					}
				}
				else
				{
					if( ::IsWindowVisible( GetHwnd() ) )
					{
						::ShowWindow( GetHwnd(), SW_HIDE );
					}
				}
			}
			else
			{
				if( !::IsWindowVisible( GetHwnd() ) )
				{
					// ::ShowWindow( GetHwnd(), SW_SHOWNA ) だと非表示から表示に切り替わるときに Z-order がおかしくなることがあるので ::SetWindowPos を使う
					::SetWindowPos( GetHwnd(), nullptr,0,0,0,0,
									SWP_SHOWWINDOW | SWP_NOACTIVATE
									| SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER );
				}
			}

			//	Aug, 21, 2000 genta
			GetDocument()->m_cAutoSaveAgent.ReloadAutoSaveParam();

			GetDocument()->OnChangeSetting();	// ビューに設定変更を反映させる
			GetDocument()->m_cDocType.SetDocumentIcon();	// Sep. 10, 2002 genta 文書アイコンの再設定

			break;
		case PM_CHANGESETTING_FONT:
			GetDocument()->OnChangeSetting( true );	// フォントで文字幅が変わるので、レイアウト再構築
			delete [] m_posSaveAry;
			m_posSaveAry = nullptr;
			break;
		case PM_CHANGESETTING_FONTSIZE:
			if( (-1 == wParam && CWM_CACHE_SHARE == GetLogfontCacheMode())
					|| GetDocument()->m_cDocType.GetDocumentType().GetIndex() == wParam ){
				// 文字幅で幅も変わるので再構築する
				// 変更中にさらに変更されると困るのでBlockingHookは無効
				GetDocument()->OnChangeSetting( true, false );
			}
			delete [] m_posSaveAry;
			m_posSaveAry = nullptr;
			break;
		case PM_CHANGESETTING_TYPE:
			cTypeNew = CDocTypeManager().GetDocumentTypeOfPath(GetDocument()->m_cDocFile.GetFilePath());
			if (GetDocument()->m_cDocType.GetDocumentType().GetIndex() == wParam
				|| cTypeNew.GetIndex() == wParam){
				GetDocument()->OnChangeSetting();

				// アウトライン解析画面処理
				bool bAnalyzed = FALSE;
#if 0
				if( /* 必要なら変更条件をここに記述する（将来用） */ )
				{
					// アウトライン解析画面の位置を現在の設定に合わせる
					bAnalyzed = m_cDlgFuncList.ChangeLayout( OUTLINE_LAYOUT_BACKGROUND );	// 外部からの変更通知と同等の扱い
				}
#endif
				if( m_cDlgFuncList.GetHwnd() && !bAnalyzed ){	// アウトラインを開いていれば再解析
					// SHOW_NORMAL: 解析方法が変化していれば再解析される。そうでなければ描画更新（変更されたカラーの適用）のみ。
					EFunctionCode nFuncCode = m_cDlgFuncList.GetFuncCodeRedraw(m_cDlgFuncList.m_nOutlineType);
					GetActiveView().GetCommander().HandleCommand( nFuncCode, true, SHOW_NORMAL, 0, 0, 0 );
				}
				if( MyGetAncestor( ::GetForegroundWindow(), GA_ROOTOWNER2 ) == GetHwnd() )
					::SetFocus( GetActiveView().GetHwnd() );	// フォーカスを戻す
			}
			break;
		case PM_CHANGESETTING_TYPE2:
			cTypeNew = CDocTypeManager().GetDocumentTypeOfPath(GetDocument()->m_cDocFile.GetFilePath());
			if (GetDocument()->m_cDocType.GetDocumentType().GetIndex() == wParam
				|| cTypeNew.GetIndex() == wParam){
				// indexのみ更新
				GetDocument()->m_cDocType.SetDocumentTypeIdx();
				// タイプが変更になった場合は適用する
				if (GetDocument()->m_cDocType.GetDocumentType().GetIndex() != wParam) {
					::SendMessage(m_hWnd, MYWM_CHANGESETTING, wParam, PM_CHANGESETTING_TYPE);
				}
			}
			break;
		case PM_PRINTSETTING:
			{
				if( m_pPrintPreview ){
					m_pPrintPreview->OnChangeSetting();
				}
			}
			break;
		default:
			break;
		}
		return 0L;
	case MYWM_SAVEEDITSTATE:
		{
			if( m_pPrintPreview ){
				// 一時的に設定を戻す
				SelectCharWidthCache( CWM_FONT_EDIT, CWM_CACHE_NEUTRAL );
			}
			// フォント変更前の座標の保存
			m_posSaveAry = SavePhysPosOfAllView();
			if( m_pPrintPreview ){
				// 設定を戻す
				SelectCharWidthCache( CWM_FONT_PRINT, CWM_CACHE_LOCAL );
			}
		}
		return 0L;
	case MYWM_SETACTIVEPANE:
		if( -1 == (int)wParam ){
			if( 0 == lParam ){
				nPane = m_cSplitterWnd.GetFirstPane();
			}else{
				nPane = m_cSplitterWnd.GetLastPane();
			}
			this->SetActivePane( nPane );
		}
		return 0L;

	case MYWM_SETCARETPOS:	/* カーソル位置変更通知 */
		{
			//	2006.07.09 genta LPARAMに新たな意味を追加
			//	bit 0 (MASK 1): (bit 1==0のとき) 0/選択クリア, 1/選択開始・変更
			//	bit 1 (MASK 2): 0: bit 0の設定に従う．1:現在の選択ロックs状態を継続
			//	既存の実装では どちらも0なので強制解除と解釈される．
			//	呼び出し時はe_PM_SETCARETPOS_SELECTSTATEの値を使うこと．
			bool bSelect = (0!= (lParam & 1));
			if( lParam & 2 ){
				// 現在の状態をKEEP
				bSelect = GetActiveView().GetSelectionInfo().m_bSelectingLock;
			}

			//	2006.07.09 genta 強制解除しない
			/*
			カーソル位置変換
			 物理位置(行頭からのバイト数、折り返し無し行位置)
			→
			 レイアウト位置(行頭からの表示桁位置、折り返しあり行位置)
			*/
			CLogicPoint* ppoCaret = &(m_pShareData->m_sWorkBuffer.m_LogicPoint);
			CLayoutPoint ptCaretPos;
			GetDocument()->m_cLayoutMgr.LogicToLayout(
				*ppoCaret,
				&ptCaretPos
			);
			// 改行の真ん中にカーソルが来ないように	// 2007.08.22 ryoji
			// Note. もとが改行単位の桁位置なのでレイアウト折り返しの桁位置を超えることはない。
			//       選択指定(bSelect==TRUE)の場合にはどうするのが妥当かよくわからないが、
			//       2007.08.22現在ではアウトライン解析ダイアログから桁位置0で呼び出される
			//       パターンしかないので実用上特に問題は無い。
			if( !bSelect ){
				const CDocLine *pTmpDocLine = GetDocument()->m_cDocLineMgr.GetLine( ppoCaret->GetY2() );
				if( pTmpDocLine ){
					if( pTmpDocLine->GetLengthWithoutEOL() < ppoCaret->x ) ptCaretPos.x--;
				}
			}
			//	2006.07.09 genta 選択範囲を考慮して移動
			//	MoveCursorの位置調整機能があるので，最終行以降への
			//	移動指示の調整もMoveCursorにまかせる
			GetActiveView().MoveCursorSelecting( ptCaretPos, bSelect, _CARETMARGINRATE / 3 );
		}
		return 0L;

	case MYWM_GETCARETPOS:	/* カーソル位置取得要求 */
		/*
		カーソル位置変換
		 レイアウト位置(行頭からの表示桁位置、折り返しあり行位置)
		→
		物理位置(行頭からのバイト数、折り返し無し行位置)
		*/
		{
			CLogicPoint* ppoCaret = &(m_pShareData->m_sWorkBuffer.m_LogicPoint);
			GetDocument()->m_cLayoutMgr.LayoutToLogic(
				GetActiveView().GetCaret().GetCaretLayoutPos(),
				ppoCaret
			);
		}
		return 0L;

	case MYWM_GETLINEDATA:	/* 行(改行単位)データの要求 */
	{
		// 共有データ：自分Write→相手Read
		// return 0以上：行データあり。wParamオフセットを除いた行データ長。0はEOFかOffsetがちょうどバッファ長だった
		//       -1以下：エラー
		CLogicInt	nLineNum = CLogicInt(wParam);
		CLogicInt	nLineOffset = CLogicInt(lParam);
		if( nLineNum < 0 || GetDocument()->m_cDocLineMgr.GetLineCount() < nLineNum ){
			return -2; // 行番号不正。LineCount == nLineNum はEOF行として下で処理
		}
		CLogicInt	nLineLen = CLogicInt(0);
		const wchar_t*	pLine = GetDocument()->m_cDocLineMgr.GetLine(nLineNum)->GetDocLineStrWithEOL( &nLineLen );
		if( nLineOffset < 0 || nLineLen < nLineOffset ){
			return -3; // オフセット位置不正
		}
		if( nLineNum == GetDocument()->m_cDocLineMgr.GetLineCount() ){
			return 0; // EOF正常終了
		}
 		if( nullptr == pLine ){
			return -4; // 不明なエラー
		}
		if( nLineLen == nLineOffset ){
 			return 0;
 		}
		pLine = GetDocument()->m_cDocLineMgr.GetLine(CLogicInt(wParam))->GetDocLineStrWithEOL( &nLineLen );
		pLine += nLineOffset;
		nLineLen -= nLineOffset;
		size_t nEnd = t_min<size_t>(nLineLen, m_pShareData->m_sWorkBuffer.GetWorkBufferCount<EDIT_CHAR>());
		wmemcpy( m_pShareData->m_sWorkBuffer.GetWorkBuffer<EDIT_CHAR>(), pLine, nEnd );
		return nLineLen;
	}
	case MYWM_GETLINECOUNT:
	{
		return GetDocument()->m_cDocLineMgr.GetLineCount();
	}

	// 2010.05.11 Moca MYWM_ADDSTRINGLEN_Wを追加 NULセーフ
	case MYWM_ADDSTRINGLEN_W:
		{
			EDIT_CHAR* pWork = m_pShareData->m_sWorkBuffer.GetWorkBuffer<EDIT_CHAR>();
			size_t addSize = t_min((size_t)wParam, m_pShareData->m_sWorkBuffer.GetWorkBufferCount<EDIT_CHAR>() );
			GetActiveView().GetCommander().HandleCommand( F_ADDTAIL_W, true, (LPARAM)pWork, (LPARAM)addSize, 0, 0 );
			GetActiveView().GetCommander().HandleCommand( F_GOFILEEND, true, 0, 0, 0, 0 );
		}
		return 0L;

	//タブウインドウ	//@@@ 2003.05.31 MIK
	case MYWM_TAB_WINDOW_NOTIFY:
		m_cTabWnd.TabWindowNotify( wParam, lParam );
		{
			RECT		rc;
			::GetClientRect( GetHwnd(), &rc );
			OnSize2( m_nWinSizeType, MAKELONG( rc.right - rc.left, rc.bottom - rc.top ), false );
			GetActiveView().SetIMECompFormPos();
		}
		return 0L;

	//アウトライン	// 2010.06.06 ryoji
	case MYWM_OUTLINE_NOTIFY:
		m_cDlgFuncList.OnOutlineNotify( wParam, lParam );
		return 0L;

	//バーの表示・非表示	//@@@ 2003.06.10 MIK
	case MYWM_BAR_CHANGE_NOTIFY:
		if( GetHwnd() != (HWND)lParam )
		{
			switch( wParam )
			{
			case MYBCN_TOOLBAR:
				LayoutToolBar();	// 2006.12.19 ryoji
				break;
			case MYBCN_FUNCKEY:
				LayoutFuncKey();	// 2006.12.19 ryoji
				break;
			case MYBCN_TAB:
				LayoutTabBar();		// 2006.12.19 ryoji
				if( m_pShareData->m_Common.m_sTabBar.m_bDispTabWnd
					&& !m_pShareData->m_Common.m_sTabBar.m_bDispTabWndMultiWin )
				{
					::ShowWindow(GetHwnd(), SW_HIDE);
				}
				else
				{
					// ::ShowWindow( hwnd, SW_SHOWNA ) だと非表示から表示に切り替わるときに Z-order がおかしくなることがあるので ::SetWindowPos を使う
					::SetWindowPos( hwnd, nullptr,0,0,0,0,
									SWP_SHOWWINDOW | SWP_NOACTIVATE
									| SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER );
				}
				break;
			case MYBCN_STATUSBAR:
				LayoutStatusBar();		// 2006.12.19 ryoji
				break;
			case MYBCN_MINIMAP:
				LayoutMiniMap();
				break;
			default:
				break;
			}
			EndLayoutBars();	// 2006.12.19 ryoji
		}
		DarkMode::setChildCtrlsSubclassAndTheme(hwnd);
		return 0L;

	//by 鬼 (2) MYWM_CHECKSYSMENUDBLCLKは不要に, WM_LBUTTONDBLCLK追加
	case WM_NCLBUTTONDOWN:
		return OnNcLButtonDown(wParam, lParam);

	case WM_NCLBUTTONUP:
		return OnNcLButtonUp(wParam, lParam);

	case WM_LBUTTONDBLCLK:
		return OnLButtonDblClk(wParam, lParam);

#if 0
	case WM_IME_NOTIFY:	// Nov. 26, 2006 genta
		if( wParam == IMN_SETCONVERSIONMODE || wParam == IMN_SETOPENSTATUS){
			GetActiveView().GetCaret().ShowEditCaret();
		}
		return DefWindowProc( hwnd, uMsg, wParam, lParam );
#endif

	case WM_NCPAINT:
		DefWindowProc( hwnd, uMsg, wParam, lParam );
		if( nullptr == m_cStatusBar.GetStatusHwnd() ){
			PrintMenubarMessage( nullptr );
		}
		return 0;

	case WM_NCACTIVATE:
		// 編集ウィンドウ切替中（タブまとめ時）はタイトルバーのアクティブ／非アクティブ状態をできるだけ変更しないように（１）	// 2007.04.03 ryoji
		// 前面にいるのが編集ウィンドウならアクティブ状態を保持する
		if( m_pShareData->m_sFlags.m_bEditWndChanging && IsSakuraMainWindow(::GetForegroundWindow()) ){
			wParam = TRUE;	// アクティブ
		}
		lRes = DefWindowProc( hwnd, uMsg, wParam, lParam );
		if( nullptr == m_cStatusBar.GetStatusHwnd() ){
			PrintMenubarMessage( nullptr );
		}
		return lRes;

	case WM_SETTEXT:
		// 編集ウィンドウ切替中（タブまとめ時）はタイトルバーのアクティブ／非アクティブ状態をできるだけ変更しないように（２）	// 2007.04.03 ryoji
		// タイマーを使用してタイトルの変更を遅延する
		if( m_pShareData->m_sFlags.m_bEditWndChanging ){
			delete[] m_pszLastCaption;
			m_pszLastCaption = new WCHAR[ ::wcslen((LPCWSTR)lParam) + 1 ];
			::wcscpy( m_pszLastCaption, (LPCWSTR)lParam );	// 変更後のタイトルを記憶しておく
			::SetTimer( GetHwnd(), IDT_CAPTION, 50, nullptr );
			return 0L;
		}
		::KillTimer( GetHwnd(), IDT_CAPTION );	// タイマーが残っていたら削除する（遅延タイトルを破棄）
		return DefWindowProc( hwnd, uMsg, wParam, lParam );

	case WM_TIMER:
		if( !OnTimer(wParam, lParam) )
			return 0L;
		return DefWindowProc( hwnd, uMsg, wParam, lParam );

	default:
#if 0
// << 20020331 aroka 再変換対応 for 95/NT
		if( uMsg == m_uMSIMEReconvertMsg || uMsg == m_uATOKReconvertMsg){
			return Views_DispatchEvent( hwnd, uMsg, wParam, lParam );
		}
// >> by aroka
#endif
		return DefWindowProc( hwnd, uMsg, wParam, lParam );
	}
}

/*! 終了時の処理

	@param hWndFrom [in] 終了要求の Wimdow Handle	//2013/4/9 Uchi

	@retval TRUE: 終了して良い / FALSE: 終了しない
*/
int	CEditWnd::OnClose(HWND hWndActive, bool bGrepNoConfirm )
{
	/* ファイルを閉じるときのMRU登録 & 保存確認 & 保存実行 */
	int nRet = GetDocument()->OnFileClose( bGrepNoConfirm );
	if( !nRet ) return nRet;

	// パラメータでハンドルを貰う様にしたので検索を削除	2013/4/9 Uchi
	if( hWndActive ){
		// アクティブ化制御ウィンドウをアクティブ化する
		if( IsSakuraMainWindow(hWndActive) ){
			ActivateFrameWindow(hWndActive);	// エディタ
		}else{
			::SetForegroundWindow(hWndActive);	// タスクトレイ
		}
	}

#if 0
	// 2005.09.01 ryoji タブまとめ表示の場合は次のウィンドウを前面に（終了時のウィンドウちらつきを抑制）
	if( m_pShareData->m_Common.m_sTabBar.m_bDispTabWnd
		&& !m_pShareData->m_Common.m_sTabBar.m_bDispTabWndMultiWin )
	{
		int i, j;
		EditNode*	p = NULL;
		int nCount = CAppNodeManager::getInstance()->GetOpenedWindowArr( &p, FALSE );
		if( nCount > 1 )
		{
			for( i = 0; i < nCount; i++ )
			{
				if( p[ i ].GetHwnd() == GetHwnd() )
					break;
			}
			if( i < nCount )
			{
				for( j = i + 1; j < nCount; j++ )
				{
					if( p[ j ].m_nGroup == p[ i ].m_nGroup )
						break;
				}
				if( j >= nCount )
				{
					for( j = 0; j < i; j++ )
					{
						if( p[ j ].m_nGroup == p[ i ].m_nGroup )
							break;
					}
				}
				if( j != i )
				{
					HWND hwnd = p[ j ].GetHwnd();
					{
						// 2006.01.28 ryoji
						// タブまとめ表示でこの画面が非表示から表示に変わってすぐ閉じる場合(タブの中クリック時等)、
						// 以前のウィンドウが消えるよりも先に一気にここまで処理が進んでしまうと
						// あとで画面がちらつくので、以前のウィンドウが消えるのをちょっとだけ待つ
						int iWait = 0;
						while( ::IsWindowVisible( hwnd ) && iWait++ < 20 )
							::Sleep(1);
					}
					if( !::IsWindowVisible( hwnd ) )
					{
						ActivateFrameWindow( hwnd );
					}
				}
			}
		}
		if( p ) delete []p;
	}
#endif	// 0

	return nRet;
}

/*! WM_COMMAND処理
	@date 2000.11.15 JEPRO //ショートカットキーがうまく働かないので殺してあった下の2行(F_HELP_CONTENTS,F_HELP_SEARCH)を修正・復活
	@date 2013.05.09 novice 重複するメッセージ処理削除
*/
void CEditWnd::OnCommand( WORD wNotifyCode, WORD wID , HWND hwndCtl )
{
	if (m_bRealtimeFilterMode && wNotifyCode == 0 &&
		(wID == F_COPY || wID == F_CUT || wID == F_PASTE || wID == F_UNDO || wID == F_REDO || wID == F_SELECTALL)) {
		const HWND focus = ::GetFocus();
		if (focus == m_hwndRealtimeFilterInclude || focus == m_hwndRealtimeFilterExclude) {
			if (wID == F_REDO) return;
			const UINT message = wID == F_COPY ? WM_COPY : wID == F_CUT ? WM_CUT :
				wID == F_PASTE ? WM_PASTE : wID == F_UNDO ? WM_UNDO : EM_SETSEL;
			if (wID == F_SELECTALL) ::SendMessageW(focus, EM_SETSEL, 0, -1);
			else ::SendMessageW(focus, message, 0, 0);
			return;
		}
	}
	if (hwndCtl && (wID == IDC_REALTIME_FILTER_INCLUDE || wID == IDC_REALTIME_FILTER_EXCLUDE)) {
		if (wNotifyCode == EN_CHANGE) ScheduleRealtimeFilter();
		return;
	}
	if (hwndCtl && (wID == IDC_REALTIME_FILTER_LOGIC || wID == IDC_REALTIME_FILTER_CONTEXT)) {
		if (wNotifyCode == CBN_SELCHANGE || wNotifyCode == CBN_EDITCHANGE) ScheduleRealtimeFilter();
		return;
	}
	if (hwndCtl && (wID == IDC_REALTIME_FILTER_CASE || wID == IDC_REALTIME_FILTER_REGEX) && wNotifyCode == BN_CLICKED) {
		ScheduleRealtimeFilter();
		return;
	}
	if (hwndCtl && wNotifyCode == BN_CLICKED) {
		switch (wID) {
		case IDC_REALTIME_FILTER_COPY: ExportRealtimeFilter(false); return;
		case IDC_REALTIME_FILTER_NEW: ExportRealtimeFilter(true); return;
		case IDC_REALTIME_FILTER_SAVEAS: ExportRealtimeFilter(true, true); return;
		case IDC_REALTIME_FILTER_CLEAR:
			::SetWindowTextW(m_hwndRealtimeFilterInclude, L"");
			::SetWindowTextW(m_hwndRealtimeFilterExclude, L"");
			::SendMessageW(m_hwndRealtimeFilterLogic, CB_SETCURSEL, 0, 0);
			::SendMessageW(m_hwndRealtimeFilterCase, BM_SETCHECK, BST_UNCHECKED, 0);
			::SendMessageW(m_hwndRealtimeFilterRegex, BM_SETCHECK, BST_UNCHECKED, 0);
			::SetWindowTextW(m_hwndRealtimeFilterContext, L"0");
			ScheduleRealtimeFilter();
			return;
		case IDC_REALTIME_FILTER_CLOSE: CloseRealtimeFilter(); return;
		case IDC_REALTIME_FILTER_CANCEL:
			::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_DEBOUNCE);
			::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_SCAN);
			++m_uRealtimeFilterToken;
			if (m_pRealtimeFilterService) m_pRealtimeFilterService->Cancel();
			m_bRealtimeFilterBatchInFlight = false;
			m_bRealtimeFilterHasPendingBatch = false;
			m_bRealtimeFilterScanComplete = false;
			m_pRealtimeFilterScanLine = nullptr;
			m_vRealtimeFilterPendingBatch.clear();
			m_vRealtimeFilterAppendCacheFingerprints.clear();
			m_vRealtimeFilterAppendCacheMatches.clear();
			m_bRealtimeFilterAppendCachePending = false;
			m_bRealtimeFilterAppendCacheActive = false;
			if (m_hwndRealtimeFilterStats) {
				const std::wstring status = L"中止しました（部分結果 " + std::to_wstring(m_vRealtimeFilterMatches.size()) + L" 行）";
				::SetWindowTextW(m_hwndRealtimeFilterStats, status.c_str());
			}
			return;
		case IDC_REALTIME_FILTER_AUTOSCROLL:
			m_bRealtimeFilterAutoScroll = ::SendMessageW(m_hwndRealtimeFilterAutoScroll, BM_GETCHECK, 0, 0) == BST_CHECKED;
			return;
		default: break;
		}
	}
	if (m_bRealtimeFilterMode && (wNotifyCode == 0 || wNotifyCode == CMD_FROM_MOUSE)) {
		switch (static_cast<EFunctionCode>(wID)) {
		case F_FILEOPEN:
		case F_FILEOPEN_DROPDOWN:
		case F_FILECLOSE_OPEN:
		case F_FILESAVE:
		case F_FILESAVEAS_DIALOG:
		case F_FILESAVEAS:
		case F_FILESAVE_QUIET:
		case F_FILESAVECLOSE:
		case F_FILECLOSE:
		case F_WINCLOSE:
		case F_FILENEW:
		case F_EXITALL:
		case F_EXITALLEDITORS:
			break;
		default:
			SendStatusMessage(L"フィルター表示中は編集できません。編集するにはフィルターを閉じてください。");
			return;
		}
		if (wID == F_FILEOPEN || wID == F_FILEOPEN_DROPDOWN || wID == F_FILECLOSE_OPEN || wID == F_FILESAVECLOSE ||
			wID == F_FILECLOSE || wID == F_WINCLOSE || wID == F_FILENEW ||
			wID == F_EXITALL || wID == F_EXITALLEDITORS) {
			CloseRealtimeFilter();
		}
	}
	// 検索ボックスからの WM_COMMAND はすべてコンボボックス通知
	// ##### 検索ボックス処理はツールバー側の WindowProc に集約するほうがスマートかも
	if( m_cToolbar.GetSearchHwnd() && hwndCtl == m_cToolbar.GetSearchHwnd() ){
		switch( wNotifyCode ){
		case CBN_SETFOCUS:
			m_nCurrentFocus = F_SEARCH_BOX;
			break;
		case CBN_KILLFOCUS:
		{
			m_nCurrentFocus = 0;
			//フォーカスがはずれたときに検索キーにしてしまう。
			//検索キーワードを取得
			std::wstring	strText;
			if( m_cToolbar.GetSearchKey(strText) )	//キー文字列がある
			{
				//検索キーを登録
				if( strText.length() < _MAX_PATH ){
					CSearchKeywordManager().AddToSearchKeyArr( strText.c_str() );
				}
				GetActiveView().m_strCurSearchKey = std::move(strText);
				GetActiveView().m_bCurSearchUpdate = true;
				GetActiveView().ChangeCurRegexp();
			}
			break;
		}
		default:
			break;
		}
		return;	// CBN_SELCHANGE(1) がアクセラレータと誤認されないようにここで抜ける（rev1886 の問題の抜本対策）
	}

	switch( wNotifyCode ){
	/* メニューからのメッセージ */
	case 0:
	case CMD_FROM_MOUSE: // 2006.05.19 genta マウスから呼びだされた場合
		//ウィンドウ切り替え
		if( wID - IDM_SELWINDOW >= 0 && wID - IDM_SELWINDOW < m_pShareData->m_sNodes.m_nEditArrNum ){
			ActivateFrameWindow( m_pShareData->m_sNodes.m_pEditArr[wID - IDM_SELWINDOW].GetHwnd() );
		}
		//最近使ったファイル
		else if( wID - IDM_SELMRU >= 0 && wID - IDM_SELMRU < 999){
			/* 指定ファイルが開かれているか調べる */
			const CMRUFile cMRU;
			EditInfo checkEditInfo;
			cMRU.GetEditInfo(wID - IDM_SELMRU, &checkEditInfo);
			SLoadInfo sLoadInfo(checkEditInfo.m_szPath, checkEditInfo.m_nCharCode, false);
			GetDocument()->m_cDocFileOperation.FileLoad( &sLoadInfo );	//	Oct.  9, 2004 genta 共通関数化
		}
		//最近使ったフォルダー
		else if( wID - IDM_SELOPENFOLDER >= 0 && wID - IDM_SELOPENFOLDER < 999){
			//フォルダー取得
			const CMRUFolder cMRUFolder;
			LPCWSTR pszFolderPath = cMRUFolder.GetPath( wID - IDM_SELOPENFOLDER );

			//Stonee, 2001/12/21 UNCであれば接続を試みる
			NetConnect( pszFolderPath );

			//「ファイルを開く」ダイアログ
			SLoadInfo sLoadInfo(L"", CODE_AUTODETECT, false);
			CDocFileOperation& cDocOp = GetDocument()->m_cDocFileOperation;
			std::vector<std::wstring> files;
			if( cDocOp.OpenFileDialog(GetHwnd(), pszFolderPath, &sLoadInfo, files) ){
				sLoadInfo.cFilePath = files[0].c_str();
				//開く
				cDocOp.FileLoad( &sLoadInfo );

				// 新たな編集ウィンドウを起動
				size_t nSize = files.size();
				for( size_t f = 1; f < nSize; f++ ){
					sLoadInfo.cFilePath = files[f].c_str();
					CControlTray::OpenNewEditor( G_AppInstance(), GetHwnd(), sLoadInfo, nullptr, true );
				}
			}
		}
		//その他コマンド
		else{
			//ビューにフォーカスを移動しておく
			if( wID != F_SEARCH_BOX && m_nCurrentFocus == F_SEARCH_BOX ) {
				::SetFocus( GetActiveView().GetHwnd() );
			}

			// コマンドコードによる処理振り分け
			//	May 19, 2006 genta 上位ビットを渡す
			//	Jul. 7, 2007 genta 上位ビットを定数に
			GetDocument()->HandleCommand( (EFunctionCode)(wID | 0) );
		}
		break;
	/* アクセラレータからのメッセージ */
	case 1:
		{
			//ビューにフォーカスを移動しておく
			if( wID != F_SEARCH_BOX && m_nCurrentFocus == F_SEARCH_BOX )
				::SetFocus( GetActiveView().GetHwnd() );

			EFunctionCode nFuncCode = CKeyBind::GetFuncCode(
				wID,
				m_pShareData->m_Common.m_sKeyBind.m_nKeyNameArrNum,
				m_pShareData->m_Common.m_sKeyBind.m_pKeyNameArr
			);
			GetDocument()->HandleCommand( (EFunctionCode)(nFuncCode | FA_FROMKEYBOARD) );
		}
		break;
	default:
		break;
	}

	return;
}

//	キーワード：メニューバー順序
//	Sept.14, 2000 Jepro note: メニューバーの項目のキャプションや順番設定などは以下で行っているらしい
//	Sept.16, 2000 Jepro note: アイコンとの関連付けはCShareData_new2.cppファイルで行っている
//	2010/5/16	Uchi	動的に作成する様に変更
void CEditWnd::InitMenu( HMENU hMenu, UINT uPos, BOOL fSystemMenu )
{
	int			cMenuItems;
	int			nPos;
	UINT		fuFlags;
	int			i;
	HMENU		hMenuPopUp;

	MENUINFO mi = { sizeof(mi) };
	mi.fMask = MIM_STYLE;
	mi.dwStyle = MNS_CHECKORBMP;
	SetMenuInfo(hMenu, &mi);

	if( hMenu == ::GetSubMenu( ::GetMenu( GetHwnd() ), uPos )
		&& !fSystemMenu ){
		// 情報取得
		const CommonSetting_MainMenu*	pcMenu = &m_pShareData->m_Common.m_sMainMenu;
		const CMainMenu*	cMainMenu;
		int			nIdxStr;
		int			nIdxEnd;
		int			nLv;
		std::vector<HMENU>	hSubMenu;
		wchar_t tmpMenuName[MAX_MAIN_MENU_NAME_LEN+1];
		const wchar_t *pMenuName;

		nIdxStr = pcMenu->m_nMenuTopIdx[uPos];
		nIdxEnd = (uPos < MAX_MAINMENU_TOP) ? pcMenu->m_nMenuTopIdx[uPos+1] : -1;
		if (nIdxEnd < 0) {
			nIdxEnd = pcMenu->m_nMainMenuNum;
		}

		// メニュー 初期化
		m_cMenuDrawer.ResetContents();
		cMenuItems = ::GetMenuItemCount( hMenu );
		for( i = cMenuItems - 1; i >= 0; i-- ){
			::DeleteMenu( hMenu, i, MF_BYPOSITION );
		}

		// メニュー作成
		hSubMenu.push_back( hMenu );
		nLv = 1;
		if (pcMenu->m_cMainMenuTbl[nIdxStr].m_nType == T_SPECIAL) {
			nLv = 0;
			nIdxStr--;
		}
		for (i = nIdxStr + 1; i < nIdxEnd; i++) {
			cMainMenu = &pcMenu->m_cMainMenuTbl[i];
			if (cMainMenu->m_nLevel != nLv) {
				nLv = cMainMenu->m_nLevel;
				if (hSubMenu.size() < (size_t)nLv) {
					// 保護
					break;
				}
				hMenu = hSubMenu[nLv-1];
			}
			switch (cMainMenu->m_nType) {
			case T_NODE:
				hMenuPopUp = ::CreatePopupMenu();
				if (cMainMenu->m_nFunc != 0 && cMainMenu->m_sName[0] == L'\0') {
					// ストリングテーブルから読み込み
					wcsncpy_s(tmpMenuName, std::size(tmpMenuName), LS( cMainMenu->m_nFunc ), _TRUNCATE);
					pMenuName = tmpMenuName;
				}else{
					pMenuName = cMainMenu->m_sName;
				}
				m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING | MF_POPUP, (UINT_PTR)hMenuPopUp ,
					pMenuName, cMainMenu->m_sKey );
				if (hSubMenu.size() > (size_t)nLv) {
					hSubMenu[nLv] = hMenuPopUp;
				}
				else {
					hSubMenu.push_back( hMenuPopUp );
				}
				break;
			case T_LEAF:
				InitMenu_Function( hMenu, cMainMenu->m_nFunc, cMainMenu->m_sName, cMainMenu->m_sKey );
				break;
			case T_SEPARATOR:
				m_cMenuDrawer.MyAppendMenuSep( hMenu, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr );
				break;
			case T_SPECIAL:
				bool	bInList;		// リストが1個以上ある
				bInList = InitMenu_Special( hMenu, cMainMenu->m_nFunc );
				// リストが無い場合の処理
				//分割線に囲まれ、かつリストなし ならば 次の分割線をスキップ
				if (!bInList &&
					i + 1 < nIdxEnd &&
					T_SEPARATOR == pcMenu->m_cMainMenuTbl[i + 1].m_nType &&
					cMainMenu->m_nLevel == pcMenu->m_cMainMenuTbl[i + 1].m_nLevel &&
					(i == nIdxStr + 1 || (0 < i && T_SEPARATOR == pcMenu->m_cMainMenuTbl[i - 1].m_nType && cMainMenu->m_nLevel == pcMenu->m_cMainMenuTbl[i - 1].m_nLevel))) {
						i++;		// スキップ
				}
				break;
			}
		}
		if (nLv > 0) {
			// レベルが戻っていない
			hMenu = hSubMenu[0];
		}
		// 子の無い設定SubMenuのDesable
		CheckFreeSubMenu( GetHwnd(), hMenu, uPos );
	}

//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
//	if (m_pPrintPreview)	return;	//	印刷プレビューモードなら排除。（おそらく排除しなくてもいいと思うんだけど、念のため）

	/* 機能が利用可能かどうか、チェック状態かどうかを一括チェック */
	cMenuItems = ::GetMenuItemCount( hMenu );
	for (nPos = 0; nPos < cMenuItems; nPos++) {
		EFunctionCode	id = (EFunctionCode)::GetMenuItemID(hMenu, nPos);
		/* 機能が利用可能か調べる */
		//	Jan.  8, 2006 genta 機能が有効な場合には明示的に再設定しないようにする．
		if( ! IsFuncEnable( GetDocument(), m_pShareData, id ) ){
			fuFlags = MF_BYCOMMAND | MF_DISABLED;
			::EnableMenuItem(hMenu, id, fuFlags);
		}

		/* 機能がチェック状態か調べる */
		if( IsFuncChecked( GetDocument(), m_pShareData, id ) ){
			fuFlags = MF_BYCOMMAND | MF_CHECKED;
			::CheckMenuItem(hMenu, id, fuFlags);
		}
		/* else{
			fuFlags = MF_BYCOMMAND | MF_UNCHECKED;
		}
		*/
	}

	return;
}

/*!	通常コマンド(Special以外)のメニューへの追加
*/
void CEditWnd::InitMenu_Function(HMENU hMenu, EFunctionCode eFunc, const wchar_t* pszName, const wchar_t* pszKey)
{
	const wchar_t* psName = nullptr;
	/* メニューラベルの作成 */
	// カスタムメニュー
	if (eFunc == F_MENU_RBUTTON
	  || (eFunc >= F_CUSTMENU_1 && eFunc <= F_CUSTMENU_24)) {
		int j;
		//	右クリックメニュー
		if (eFunc == F_MENU_RBUTTON) {
			j = CUSTMENU_INDEX_FOR_RBUTTONUP;
		}
		else {
			j = eFunc - F_CUSTMENU_BASE;
		}

		int nFlag = MF_BYPOSITION | MF_STRING | MF_GRAYED;
		if( m_pShareData->m_Common.m_sCustomMenu.m_nCustMenuItemNumArr[j] > 0 ){
			nFlag = MF_BYPOSITION | MF_STRING;
		}
		WCHAR buf[ MAX_CUSTOM_MENU_NAME_LEN + 1 ];
		m_cMenuDrawer.MyAppendMenu( hMenu, nFlag,
			eFunc, GetDocument()->m_cFuncLookup.Custmenu2Name( j, buf, int(std::size(buf)) ), pszKey );
	}
	// マクロ
	else if (eFunc >= F_USERMACRO_0 && eFunc < F_USERMACRO_0 + (int)MAX_CUSTMACRO) {
		MacroRec *mp = &m_pShareData->m_Common.m_sMacro.m_MacroTable[eFunc - F_USERMACRO_0];
		if (mp->IsEnabled()) {
			psName = mp->m_szName[0] ? mp->m_szName : mp->m_szFile;
			m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING,
				eFunc, psName, pszKey );
		}
		else {
			psName = L"-- undefined macro --";
			m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING | MF_GRAYED,
				eFunc, psName, pszKey );
		}
	}
	// プラグインコマンド
	else if (eFunc >= F_PLUGCOMMAND_FIRST && eFunc < F_PLUGCOMMAND_LAST) {
		WCHAR szLabel[256];
		if( 0 < CJackManager::getInstance()->GetCommandName( eFunc, szLabel, int(std::size(szLabel)) ) ){
			m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING,
				eFunc, szLabel, pszKey,
				TRUE, eFunc );
		}else{
			// not found
			psName = L"-- undefined plugin command --";
			m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING | MF_GRAYED,
				eFunc, psName, pszKey );
		}
	}else{
		switch (eFunc) {
		case F_RECKEYMACRO:
		case F_SAVEKEYMACRO:
		case F_LOADKEYMACRO:
		case F_EXECKEYMACRO:
			SetMenuFuncSel( hMenu, eFunc, pszKey,
				!m_pShareData->m_sFlags.m_bRecordingKeyMacro);
			break;
		case F_SPLIT_V:
			SetMenuFuncSel( hMenu, eFunc, pszKey,
				m_cSplitterWnd.GetAllSplitRows() == 1 );
			break;
		case F_SPLIT_H:
			SetMenuFuncSel( hMenu, eFunc, pszKey,
				m_cSplitterWnd.GetAllSplitCols() == 1 );
			break;
		case F_SPLIT_VH:
			SetMenuFuncSel( hMenu, eFunc, pszKey,
				m_cSplitterWnd.GetAllSplitRows() == 1 || m_cSplitterWnd.GetAllSplitCols() == 1 );
			break;
		case F_TAB_CLOSEOTHER:
			SetMenuFuncSel( hMenu, eFunc, pszKey,
				m_pShareData->m_Common.m_sTabBar.m_bDispTabWnd != 0 );
			break;
		case F_TOPMOST:
			SetMenuFuncSel( hMenu, eFunc, pszKey,
				((DWORD)::GetWindowLongPtr( GetHwnd(), GWL_EXSTYLE ) & WS_EX_TOPMOST) == 0 );
			break;
		case F_BIND_WINDOW:
			SetMenuFuncSel( hMenu, eFunc, pszKey,
				(!m_pShareData->m_Common.m_sTabBar.m_bDispTabWnd
				|| m_pShareData->m_Common.m_sTabBar.m_bDispTabWndMultiWin) );
			break;
		case F_SHOWTOOLBAR:
			SetMenuFuncSel( hMenu, eFunc, pszKey,
				!m_pShareData->m_Common.m_sWindow.m_bMenuIcon | !m_cToolbar.GetToolbarHwnd() );
			break;
		case F_SHOWFUNCKEY:
			SetMenuFuncSel( hMenu, eFunc, pszKey,
				!m_pShareData->m_Common.m_sWindow.m_bMenuIcon | !m_cFuncKeyWnd.GetHwnd() );
			break;
		case F_SHOWTAB:
			SetMenuFuncSel( hMenu, eFunc, pszKey,
				!m_pShareData->m_Common.m_sWindow.m_bMenuIcon | !m_cTabWnd.GetHwnd() );
			break;
		case F_SHOWSTATUSBAR:
			SetMenuFuncSel( hMenu, eFunc, pszKey,
				!m_pShareData->m_Common.m_sWindow.m_bMenuIcon | !m_cStatusBar.GetStatusHwnd() );
			break;
		case F_SHOWMINIMAP:
			SetMenuFuncSel( hMenu, eFunc, pszKey,
				!m_pShareData->m_Common.m_sWindow.m_bMenuIcon | !m_cMiniMapView.GetHwnd() );
			break;
		case F_TOGGLE_KEY_SEARCH:
			SetMenuFuncSel( hMenu, eFunc, pszKey,
				!m_pShareData->m_Common.m_sWindow.m_bMenuIcon | !IsFuncChecked( GetDocument(), m_pShareData, F_TOGGLE_KEY_SEARCH ) );
			break;
		case F_WRAPWINDOWWIDTH:
			{
				CKetaXInt ketas;
				WCHAR*	pszLabel;
				CEditView::TOGGLE_WRAP_ACTION mode = GetActiveView().GetWrapMode( &ketas );
				if( mode == CEditView::TGWRAP_NONE ){
					m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING | MF_GRAYED, F_WRAPWINDOWWIDTH , L"", pszKey );
				}
				else {
					WCHAR szBuf[60];
					pszLabel = szBuf;
					if( mode == CEditView::TGWRAP_FULL ){
						auto_sprintf(
							szBuf,
							LS( STR_WRAP_WIDTH_FULL ),	//L"折り返し桁数: %d 桁（最大）",
							MAXLINEKETAS
						);
					}
					else if( mode == CEditView::TGWRAP_WINDOW ){
						auto_sprintf(
							szBuf,
							LS( STR_WRAP_WIDTH_WINDOW ),	//L"折り返し桁数: %d 桁（右端）",
							int((Int)GetActiveView().ViewColNumToWrapColNum(
								GetActiveView().GetTextArea().m_nViewColNum
							))
						);
					}
					else {
						auto_sprintf(
							szBuf,
							LS( STR_WRAP_WIDTH_FIXED ),	//L"折り返し桁数: %d 桁（指定）",
							int((Int)GetDocument()->m_cDocType.GetDocumentAttribute().m_nMaxLineKetas)
						);
					}
					m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING, F_WRAPWINDOWWIDTH , pszLabel, pszKey );
				}
			}
			break;
		default:
			m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING, eFunc,
				pszName, pszKey );
			break;
		}
	}
}

/*!	Specialコマンドのメニューへの追加
*/
bool CEditWnd::InitMenu_Special(HMENU hMenu, EFunctionCode eFunc)
{
	int j;
	bool bInList = false;
	switch (eFunc) {
	case F_WINDOW_LIST:				// ウィンドウリスト
		{
			EditNode*	pEditNodeArr;
			int nRowNum = CAppNodeManager::getInstance()->GetOpenedWindowArr( &pEditNodeArr, TRUE );
			WinListMenu(hMenu, pEditNodeArr, nRowNum, false);
			bInList = (nRowNum > 0);
			delete [] pEditNodeArr;
		}
		break;
	case F_FILE_USED_RECENTLY:		// 最近使ったファイル
		/* MRUリストのファイルのリストをメニューにする */
		{
			//@@@ 2001.12.26 YAZAKI MRUリストは、CMRUに依頼する
			const CMRUFile cMRU;
			cMRU.CreateMenu( hMenu, &m_cMenuDrawer );	//	ファイルメニュー
			bInList = (cMRU.MenuLength() > 0);
		}
		break;
	case F_FOLDER_USED_RECENTLY:	// 最近使ったフォルダー
		/* 最近使ったフォルダーのメニューを作成 */
		{
			//@@@ 2001.12.26 YAZAKI OPENFOLDERリストは、CMRUFolderにすべて依頼する
			const CMRUFolder cMRUFolder;
			cMRUFolder.CreateMenu( hMenu, &m_cMenuDrawer );
			bInList = (cMRUFolder.MenuLength() > 0);
		}
		break;
	case F_CUSTMENU_LIST:			// カスタムメニューリスト
		WCHAR buf[ MAX_CUSTOM_MENU_NAME_LEN + 1 ];
		//	右クリックメニュー
		if( m_pShareData->m_Common.m_sCustomMenu.m_nCustMenuItemNumArr[0] > 0 ){
			 m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING,
				 F_MENU_RBUTTON, GetDocument()->m_cFuncLookup.Custmenu2Name( 0, buf, int(std::size(buf)) ), L"" );
			bInList = true;
		}
		//	カスタムメニュー
		for( j = 1; j < MAX_CUSTOM_MENU; ++j ){
			if( m_pShareData->m_Common.m_sCustomMenu.m_nCustMenuItemNumArr[j] > 0 ){
				 m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING,
			 		F_CUSTMENU_BASE + j, GetDocument()->m_cFuncLookup.Custmenu2Name( j, buf, int(std::size(buf)) ), L""  );
				bInList = true;
			}
		}
		break;
	case F_USERMACRO_LIST:			// 登録済みマクロリスト
		for( j = 0; j < MAX_CUSTMACRO; ++j ){
			MacroRec *mp = &m_pShareData->m_Common.m_sMacro.m_MacroTable[j];
			if( mp->IsEnabled() ){
				if(  mp->m_szName[0] ){
					m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING, F_USERMACRO_0 + j, mp->m_szName, L"" );
				}
				else {
					m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING, F_USERMACRO_0 + j, mp->m_szFile, L"" );
				}
				bInList = true;
			}
		}
		break;
	case F_PLUGIN_LIST:				// プラグインコマンドリスト
		//プラグインコマンドを提供するプラグインを列挙する
		{
			const CJackManager* pcJackManager = CJackManager::getInstance();
			const CPlugin* prevPlugin = nullptr;
			HMENU hMenuPlugin = nullptr;

			CPlug::Array plugs = pcJackManager->GetPlugs( PP_COMMAND );
			for( CPlug::ArrayIter it = plugs.cbegin(); it != plugs.cend(); it++ ){
				const CPlugin* curPlugin = &(*it)->m_cPlugin;
				if( curPlugin != prevPlugin ){
					//プラグインが変わったらプラグインポップアップメニューを登録
					hMenuPlugin = ::CreatePopupMenu();
					m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING | MF_POPUP, (UINT_PTR)hMenuPlugin, curPlugin->m_sName.c_str(), L"" );
					prevPlugin = curPlugin;
				}

				//コマンドを登録
				m_cMenuDrawer.MyAppendMenu( hMenuPlugin, MF_BYPOSITION | MF_STRING,
					(*it)->GetFunctionCode(), (*it)->m_sLabel.c_str(), L"",
					TRUE, (*it)->GetFunctionCode() );
			}
			bInList = (prevPlugin != nullptr);
		}
		break;
	default:
		break;
	}
	return bInList;
}

// メニューバーの無効化を検査	2010/6/18 Uchi
void CEditWnd::CheckFreeSubMenu( HWND hWnd, HMENU hMenu, UINT uPos )
{
	int 	cMenuItems;

	cMenuItems = ::GetMenuItemCount( hMenu );
	if (cMenuItems == 0) {
		// 下が無いので無効化
		::EnableMenuItem( ::GetMenu( hWnd ), uPos, MF_BYPOSITION | MF_GRAYED );
	}
	else {
		// 下位レベルを検索
		CheckFreeSubMenuSub( hMenu, 1 );
	}
}

// メニューバーの無効化を検査	2010/6/18 Uchi
void CEditWnd::CheckFreeSubMenuSub( HMENU hMenu, int nLv )
{
	HMENU	hSubMenu;
	int 	cMenuItems;
	int 	nPos;

	cMenuItems = ::GetMenuItemCount( hMenu );
	for (nPos = 0; nPos < cMenuItems; nPos++) {
		hSubMenu = ::GetSubMenu( hMenu, nPos );
		if (hSubMenu != nullptr) {
			if ( ::GetMenuItemCount( hSubMenu ) == 0) {
				// 下が無いので無効化
				::EnableMenuItem(hMenu, nPos, MF_BYPOSITION | MF_GRAYED);
			}
			else {
				// 下位レベルを検索
				CheckFreeSubMenuSub( hSubMenu, nLv + 1 );
			}
		}
	}
}

//	フラグにより表示文字列の選択をする。
//		2010/5/19	Uchi
void CEditWnd::SetMenuFuncSel( HMENU hMenu, EFunctionCode nFunc, const WCHAR* sKey, bool flag )
{
	int				i;
	const WCHAR*	sName = L"";
	for (i = 0; i < int(std::size(sFuncMenuName)) ;i++) {
		if (sFuncMenuName[i].eFunc == nFunc) {
			sName = flag ? LS( sFuncMenuName[i].nNameId[0] ) : LS( sFuncMenuName[i].nNameId[1] );
		}
	}
	assert( wcslen(sName) );

	m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING, nFunc, sName, sKey );
}

STDMETHODIMP CEditWnd::DragEnter(
	LPDATAOBJECT pDataObject,
	DWORD dwKeyState,
	POINTL pt [[maybe_unused]],
	LPDWORD pdwEffect
) const
{
	if( pDataObject == nullptr || pdwEffect == nullptr ){
		return E_INVALIDARG;
	}

	// 右ボタンファイルドロップの場合だけ処理する
	if( !((MK_RBUTTON & dwKeyState) && IsDataAvailable(pDataObject, CF_HDROP)) ){
		*pdwEffect = DROPEFFECT_NONE;
		return E_INVALIDARG;
	}

	// 印刷プレビューでは受け付けない
	if( m_pPrintPreview ){
		*pdwEffect = DROPEFFECT_NONE;
		return E_INVALIDARG;
	}

	*pdwEffect &= DROPEFFECT_LINK;
	return S_OK;
}

STDMETHODIMP CEditWnd::DragOver(
	DWORD dwKeyState [[maybe_unused]],
	POINTL pt [[maybe_unused]],
	LPDWORD pdwEffect
) const
{
	if( pdwEffect == nullptr )
		return E_INVALIDARG;

	*pdwEffect &= DROPEFFECT_LINK;
	return S_OK;
}

STDMETHODIMP CEditWnd::DragLeave() const
{
	return S_OK;
}

STDMETHODIMP CEditWnd::Drop(LPDATAOBJECT pDataObject, [[maybe_unused]] DWORD dwKeyState, [[maybe_unused]] POINTL pt, LPDWORD pdwEffect)
{
	if( pDataObject == nullptr || pdwEffect == nullptr )
		return E_INVALIDARG;

	// ファイルドロップをアクティブビューで処理する
	*pdwEffect &= DROPEFFECT_LINK;
	return GetActiveView().PostMyDropFiles( pDataObject );
}

/* ファイルがドロップされた */
void CEditWnd::OnDropFiles( HDROP hDrop )
{
	POINT		pt;
	int			cFiles, i;
	EditInfo*	pfi;
	HWND		hWndOwner;

	::DragQueryPoint( hDrop, &pt );
	cFiles = (int)::DragQueryFile( hDrop, 0xFFFFFFFF, nullptr, 0);
	/* ファイルをドロップしたときは閉じて開く */
	if( m_pShareData->m_Common.m_sFile.m_bDropFileAndClose ){
		cFiles = 1;
	}
	/* 一度にドロップ可能なファイル数 */
	if( cFiles > m_pShareData->m_Common.m_sFile.m_nDropFileNumMax ){
		cFiles = m_pShareData->m_Common.m_sFile.m_nDropFileNumMax;
	}

	/* アクティブにする */	// 2009.08.20 ryoji 処理開始前に無条件でアクティブ化
	ActivateFrameWindow( GetHwnd() );

	for( i = 0; i < cFiles; i++ ) {
		//ファイルパス取得、解決。
		WCHAR		szFile[_MAX_PATH + 1];
		::DragQueryFile( hDrop, i, szFile, int(std::size(szFile)) );
		CSakuraEnvironment::ResolvePath(szFile);

		/* 指定ファイルが開かれているか調べる */
		if( CShareData::getInstance()->IsPathOpened( szFile, &hWndOwner ) ){
			::SendMessage( hWndOwner, MYWM_GETFILEINFO, 0, 0 );
			pfi = (EditInfo*)&m_pShareData->m_sWorkBuffer.m_EditInfo_MYWM_GETFILEINFO;
			/* アクティブにする */
			ActivateFrameWindow( hWndOwner );
			/* MRUリストへの登録 */
			CMRUFile cMRU;
			cMRU.Add( pfi );
		}
		else{
			/* 変更フラグがオフで、ファイルを読み込んでいない場合 */
			//	2005.06.24 Moca
			if( GetDocument()->IsAcceptLoad() ){
				/* ファイル読み込み */
				SLoadInfo sLoadInfo(szFile, CODE_AUTODETECT, false);
				GetDocument()->m_cDocFileOperation.FileLoad(&sLoadInfo);
			}
			else{
				/* ファイルをドロップしたときは閉じて開く */
				if( m_pShareData->m_Common.m_sFile.m_bDropFileAndClose ){
					/* ファイル読み込み */
					SLoadInfo sLoadInfo(szFile, CODE_AUTODETECT, false);
					GetDocument()->m_cDocFileOperation.FileCloseOpen(sLoadInfo);
				}
				else{
					/* 編集ウィンドウの上限チェック */
					if( m_pShareData->m_sNodes.m_nEditArrNum >= MAX_EDITWINDOWS ){	//最大値修正	//@@@ 2003.05.31 MIK
						::DragFinish( hDrop );
						OkMessage( nullptr, LS(STR_MAXWINDOW), MAX_EDITWINDOWS );
						return;
					}
					/* 新たな編集ウィンドウを起動 */
					SLoadInfo sLoadInfo;
					sLoadInfo.cFilePath = szFile;
					sLoadInfo.eCharCode = CODE_NONE;
					sLoadInfo.bViewMode = false;
					CControlTray::OpenNewEditor(
						G_AppInstance(),
						GetHwnd(),
						sLoadInfo
					);
				}
			}
		}
	}
	::DragFinish( hDrop );
	return;
}

/*! WM_TIMER 処理
	@date 2007.04.03 ryoji 新規
	@date 2008.04.19 ryoji IDT_FIRST_IDLE での MYWM_FIRST_IDLE ポスト処理を追加
	@date 2013.06.09 novice コントロールプロセスへの MYWM_FIRST_IDLE ポスト処理を追加
*/
LRESULT CEditWnd::OnTimer( WPARAM wParam, [[maybe_unused]] LPARAM lParam )
{
	// タイマー ID で処理を振り分ける
	switch( wParam )
	{
	case IDT_CURL_SYNTAX:
		::KillTimer(GetHwnd(), IDT_CURL_SYNTAX);
		RunCurlSyntaxCheck();
		break;
	case IDT_CSV_SELECTION_STATS:
		::KillTimer(GetHwnd(), IDT_CSV_SELECTION_STATS);
		UpdateCSVSelectionStatistics();
		break;
	case IDT_REALTIME_FILTER_DEBOUNCE:
		::KillTimer(GetHwnd(), IDT_REALTIME_FILTER_DEBOUNCE);
		StartRealtimeFilterScan();
		break;
	case IDT_REALTIME_FILTER_SCAN:
		ContinueRealtimeFilterScan();
		break;
	case IDT_EDIT:
		OnEditTimer();
		break;
	case IDT_TOOLBAR:
		m_cToolbar.OnToolbarTimer();
		break;
	case IDT_CAPTION:
		OnCaptionTimer();
		break;
	case IDT_SYSMENU:
		OnSysMenuTimer();
		break;
	case IDT_FIRST_IDLE:
		m_cDlgFuncList.m_bEditWndReady = true;	// エディタ画面の準備完了
		CAppNodeGroupHandle(0).PostMessageToAllEditors( MYWM_FIRST_IDLE, ::GetCurrentProcessId(), 0, nullptr );	// プロセスの初回アイドリング通知	// 2008.04.19 ryoji
		::PostMessage( m_pShareData->m_sHandles.m_hwndTray, MYWM_FIRST_IDLE, (WPARAM)::GetCurrentProcessId(), (LPARAM)0 );
		::KillTimer( m_hWnd, wParam );
		break;
	default:
		return 1L;
	}

	return 0L;
}

/*! キャプション更新用タイマーの処理
	@date 2007.04.03 ryoji 新規
*/
void CEditWnd::OnCaptionTimer() const
{
	// 編集画面の切替（タブまとめ時）が終わっていたらタイマーを終了してタイトルバーを更新する
	// まだ切替中ならタイマー継続
	if( !m_pShareData->m_sFlags.m_bEditWndChanging ){
		::KillTimer( GetHwnd(), IDT_CAPTION );
		::SetWindowText( GetHwnd(), m_pszLastCaption );
	}
}

/*! システムメニュー表示用タイマーの処理
	@date 2007.04.03 ryoji パラメータ無しにした
	                       以前はコールバック関数でやっていたKillTimer()をここで行うようにした
*/
void CEditWnd::OnSysMenuTimer( void ) //by 鬼(2)
{
	::KillTimer( GetHwnd(), IDT_SYSMENU );	// 2007.04.03 ryoji

	if(m_IconClicked == icClicked)
	{
		ReleaseCapture();

		//システムメニュー表示
		// 2006.04.21 ryoji マルチモニタ対応の修正
		// 2007.05.13 ryoji 0x0313メッセージをポストする方式に変更（TrackPopupMenuだとメニュー項目の有効／無効状態が不正になる問題対策）
		RECT R;
		GetWindowRect(GetHwnd(), &R);
		POINT pt;
		pt.x = R.left + GetSystemMetrics(SM_CXFRAME);
		pt.y = R.top + GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYFRAME);
		GetMonitorWorkRect( pt, &R );
		::PostMessageAny(
			GetHwnd(),
			0x0313, //右クリックでシステムメニューを表示する際に送信するモノらしい
			0,
			MAKELPARAM( (pt.x > R.left)? pt.x: R.left, (pt.y < R.bottom)? pt.y: R.bottom )
		);
	}
	m_IconClicked = icNone;
}

//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更

/* 印刷プレビューモードのオン/オフ */
void CEditWnd::PrintPreviewModeONOFF( void )
{
	HMENU	hMenu;
	HWND	hwndToolBar;

	// 2006.06.17 ryoji Rebar があればそれをツールバー扱いする
	hwndToolBar = (nullptr != m_cToolbar.GetRebarHwnd())? m_cToolbar.GetRebarHwnd(): m_cToolbar.GetToolbarHwnd();

	/* 印刷プレビューモードか */
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
	if( m_pPrintPreview ){
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
		/*	印刷プレビューモードを解除します。	*/
		m_pPrintPreview = nullptr;	//	NULLか否かで、プリントプレビューモードか判断するため。

		/*	通常モードに戻す	*/
		::ShowWindow( this->m_cSplitterWnd.GetHwnd(), SW_SHOW );
		::ShowWindow( hwndToolBar, SW_SHOW );	// 2006.06.17 ryoji
		::ShowWindow( m_cStatusBar.GetStatusHwnd(), SW_SHOW );
		::ShowWindow( m_cFuncKeyWnd.GetHwnd(), SW_SHOW );
		::ShowWindow( m_cTabWnd.GetHwnd(), SW_SHOW );	//@@@ 2003.06.25 MIK
		::ShowWindow( m_cDlgFuncList.GetHwnd(), SW_SHOW );	// 2010.06.25 ryoji
		if( m_cMiniMapView.GetHwnd() ){
			::ShowWindow( m_cMiniMapView.GetHwnd(), SW_SHOW );
		}

		// その他のモードレスダイアログも戻す	// 2010.06.25 ryoji
		::ShowWindow( m_cDlgFind.GetHwnd(), SW_SHOW );
		::ShowWindow( m_cDlgReplace.GetHwnd(), SW_SHOW );
		::ShowWindow( m_cDlgGrep.GetHwnd(), SW_SHOW );

		::SetFocus( GetHwnd() );

		// メニューを動的に作成するように変更
		//hMenu = ::LoadMenu( G_AppInstance(), MAKEINTRESOURCE( IDR_MENU1 ) );
		//::SetMenu( GetHwnd(), hMenu );
		//::DrawMenuBar( GetHwnd() );
		LayoutMainMenu();				// 2010/5/16 Uchi

//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
		::InvalidateRect( GetHwnd(), nullptr, TRUE );
	}else{
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
		/*	通常モードを隠す	*/
		hMenu = ::GetMenu( GetHwnd() );
		//	Jun. 18, 2001 genta Print Previewではメニューを削除
		::SetMenu( GetHwnd(), nullptr );
		::DestroyMenu( hMenu );
		::DrawMenuBar( GetHwnd() );

		::ShowWindow( this->m_cSplitterWnd.GetHwnd(), SW_HIDE );
		::ShowWindow( hwndToolBar, SW_HIDE );	// 2006.06.17 ryoji
		::ShowWindow( m_cStatusBar.GetStatusHwnd(), SW_HIDE );
		::ShowWindow( m_cFuncKeyWnd.GetHwnd(), SW_HIDE );
		::ShowWindow( m_cTabWnd.GetHwnd(), SW_HIDE );	//@@@ 2003.06.25 MIK
		::ShowWindow( m_cDlgFuncList.GetHwnd(), SW_HIDE );	// 2010.06.25 ryoji
		if( m_cMiniMapView.GetHwnd() ){
			::ShowWindow( m_cMiniMapView.GetHwnd(), SW_HIDE );
		}

		// その他のモードレスダイアログも隠す	// 2010.06.25 ryoji
		::ShowWindow( m_cDlgFind.GetHwnd(), SW_HIDE );
		::ShowWindow( m_cDlgReplace.GetHwnd(), SW_HIDE );
		::ShowWindow( m_cDlgGrep.GetHwnd(), SW_HIDE );

//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
		m_pPrintPreview = std::make_unique<CPrintPreview>(this);
		/* 現在の印刷設定 */
		m_pPrintPreview->SetPrintSetting(
			&m_pShareData->m_PrintSettingArr[
				GetDocument()->m_cDocType.GetDocumentAttribute().m_nCurrentPrintSetting]
		);

		//	プリンターの情報を取得。

		/* 現在のデフォルトプリンターの情報を取得 */
		BOOL bRes;
		bRes = m_pPrintPreview->GetDefaultPrinterInfo();
		if( !bRes ){
			TopInfoMessage( GetHwnd(), LS(STR_ERR_DLGEDITWND14) );
			return;
		}

		/* 印刷設定の反映 */
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
		m_pPrintPreview->OnChangePrintSetting();
		::InvalidateRect( GetHwnd(), nullptr, TRUE );
		::UpdateWindow( GetHwnd() /* m_pPrintPreview->GetPrintPreviewBarHANDLE() */);
	}
	return;
}

/* WM_SIZE 処理 */
LRESULT CEditWnd::OnSize( WPARAM wParam, LPARAM lParam )
{
	return OnSize2(wParam, lParam, true);
}

LRESULT CEditWnd::OnSize2( WPARAM wParam, LPARAM lParam, bool bUpdateStatus )
{
	HWND		hwndToolBar;
	int			cx;
	int			cy;
	int			nToolBarHeight;
	int			nStatusBarHeight;
	int			nFuncKeyWndHeight;
	int			nTabWndHeight;	//タブウインドウ	//@@@ 2003.05.31 MIK
	RECT		rc, rcClient;
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる
//	変数削除

	RECT		rcWin;

	cx = LOWORD( lParam );
	cy = HIWORD( lParam );

	/* ウィンドウサイズ継承 */
	if( wParam != SIZE_MINIMIZED ){						/* 最小化は継承しない */
		//	2004.05.13 Moca m_eSaveWindowSizeの解釈追加のため
		if( WINSIZEMODE_SAVE == m_pShareData->m_Common.m_sWindow.m_eSaveWindowSize ){		/* ウィンドウサイズ継承をするか */
			if( wParam == SIZE_MAXIMIZED ){					/* 最大化はサイズを記録しない */
				if( m_pShareData->m_Common.m_sWindow.m_nWinSizeType != (int)wParam ){
					m_pShareData->m_Common.m_sWindow.m_nWinSizeType = (int)wParam;
				}
			}else{
				// Aero Snapの縦方向最大化状態で終了して次回起動するときは元のサイズにする必要があるので、
				// GetWindowRect()ではなくGetWindowPlacement()で得たワークエリア座標をスクリーン座標に変換して記憶する	// 2009.09.02 ryoji
				WINDOWPLACEMENT wp;
				wp.length = sizeof(wp);
				::GetWindowPlacement( GetHwnd(), &wp );	// ワークエリア座標
				rcWin = wp.rcNormalPosition;
				RECT rcWork, rcMon;
				GetMonitorWorkRect( GetHwnd(), &rcWork, &rcMon );
				::OffsetRect(&rcWin, rcWork.left - rcMon.left, rcWork.top - rcMon.top);	// スクリーン座標に変換
				/* ウィンドウサイズに関するデータが変更されたか */
				if( m_pShareData->m_Common.m_sWindow.m_nWinSizeType != (int)wParam ||
					m_pShareData->m_Common.m_sWindow.m_nWinSizeCX != rcWin.right - rcWin.left ||
					m_pShareData->m_Common.m_sWindow.m_nWinSizeCY != rcWin.bottom - rcWin.top
				){
					m_pShareData->m_Common.m_sWindow.m_nWinSizeType = (int)wParam;
					m_pShareData->m_Common.m_sWindow.m_nWinSizeCX = rcWin.right - rcWin.left;
					m_pShareData->m_Common.m_sWindow.m_nWinSizeCY = rcWin.bottom - rcWin.top;
				}
			}
		}

		// 元に戻すときのサイズ種別を記憶	// 2007.06.20 ryoji
		EditNode *p = CAppNodeManager::getInstance()->GetEditNode( GetHwnd() );
		if( p != nullptr ){
			p->m_showCmdRestore = ::IsZoomed( p->GetHwnd() )? SW_SHOWMAXIMIZED: SW_SHOWNORMAL;
		}
	}

	m_nWinSizeType = (int)wParam;	/* サイズ変更のタイプ */

	// 2006.06.17 ryoji Rebar があればそれをツールバー扱いする
	hwndToolBar = (nullptr != m_cToolbar.GetRebarHwnd())? m_cToolbar.GetRebarHwnd(): m_cToolbar.GetToolbarHwnd();
	nToolBarHeight = 0;
	if( nullptr != hwndToolBar ){
		::SendMessage( hwndToolBar, WM_SIZE, wParam, lParam );
		::GetWindowRect( hwndToolBar, &rc );
		nToolBarHeight = rc.bottom - rc.top;
	}
	if (m_bRealtimeFilterMode && m_hwndRealtimeFilterBar) {
		const int filterHeight = DpiScaleY(150);
		const int panelTop = nToolBarHeight;
		::MoveWindow(m_hwndRealtimeFilterBar, 0, panelTop, cx, filterHeight, TRUE);
		const int margin = DpiScaleX(8);
		const int labelWidth = DpiScaleX(112);
		const int editLeft = margin + labelWidth;
		const int editWidth = static_cast<int>(std::clamp<LONG>(static_cast<LONG>(cx * 34 / 100), DpiScaleX(180), DpiScaleX(520)));
		const int smallGap = DpiScaleX(8);
		const int rowHeight = DpiScaleY(32);
		const int lineEditHeight = DpiScaleY(46);
		const int row1 = panelTop + DpiScaleY(4);
		const int row2 = panelTop + DpiScaleY(54);
		const int row3 = panelTop + DpiScaleY(111);
		auto moveControl = [this](int id, int x, int y, int width, int height) {
			if (HWND control = ::GetDlgItem(GetHwnd(), id)) ::MoveWindow(control, x, y, width, height, TRUE);
		};
		moveControl(IDC_REALTIME_FILTER_INCLUDE_LABEL, margin, row1 + DpiScaleY(5), labelWidth - smallGap, DpiScaleY(20));
		moveControl(IDC_REALTIME_FILTER_INCLUDE, editLeft, row1, editWidth, lineEditHeight);
		int x = editLeft + editWidth + smallGap;
		moveControl(IDC_REALTIME_FILTER_LOGIC, x, row1, DpiScaleX(82), rowHeight);
		x += DpiScaleX(88);
		moveControl(IDC_REALTIME_FILTER_CASE, x, row1, DpiScaleX(145), rowHeight);
		x += DpiScaleX(150);
		moveControl(IDC_REALTIME_FILTER_REGEX, x, row1, DpiScaleX(96), rowHeight);
		moveControl(IDC_REALTIME_FILTER_EXCLUDE_LABEL, margin, row2 + DpiScaleY(5), labelWidth - smallGap, DpiScaleY(20));
		moveControl(IDC_REALTIME_FILTER_EXCLUDE, editLeft, row2, editWidth, lineEditHeight);
		x = editLeft + editWidth + smallGap;
		moveControl(IDC_REALTIME_FILTER_CONTEXT_LABEL, x, row2 + DpiScaleY(5), DpiScaleX(70), DpiScaleY(20));
		x += DpiScaleX(72);
		moveControl(IDC_REALTIME_FILTER_CONTEXT, x, row2, DpiScaleX(92), rowHeight);
		x += DpiScaleX(100);
		moveControl(IDC_REALTIME_FILTER_STATS, x, row2 + DpiScaleY(1), static_cast<int>(std::max<LONG>(DpiScaleX(80), static_cast<LONG>(cx - x - margin))), rowHeight - DpiScaleY(2));
		moveControl(IDC_REALTIME_FILTER_AUTOSCROLL, margin, row3 + DpiScaleY(1), DpiScaleX(190), DpiScaleY(24));
		const int buttonHeight = DpiScaleY(24);
		const int buttonWidths[] = { DpiScaleX(105), DpiScaleX(116), DpiScaleX(88), DpiScaleX(92), DpiScaleX(86), DpiScaleX(72) };
		const int buttonIds[] = { IDC_REALTIME_FILTER_COPY, IDC_REALTIME_FILTER_NEW,
			IDC_REALTIME_FILTER_SAVEAS, IDC_REALTIME_FILTER_CLEAR, IDC_REALTIME_FILTER_CANCEL, IDC_REALTIME_FILTER_CLOSE };
		int buttonX = cx - margin;
		for (int index = static_cast<int>(std::size(buttonIds)) - 1; index >= 0; --index) {
			buttonX -= buttonWidths[index];
			moveControl(buttonIds[index], buttonX, row3, buttonWidths[index], buttonHeight);
			buttonX -= smallGap;
		}
		nToolBarHeight += filterHeight;
	}
	nFuncKeyWndHeight = 0;
	if( nullptr != m_cFuncKeyWnd.GetHwnd() ){
		::SendMessage( m_cFuncKeyWnd.GetHwnd(), WM_SIZE, wParam, lParam );
		::GetWindowRect( m_cFuncKeyWnd.GetHwnd(), &rc );
		nFuncKeyWndHeight = rc.bottom - rc.top;
	}
	//@@@ From Here 2003.05.31 MIK
	//@@@ To Here 2003.05.31 MIK
	bool bMiniMapSizeBox = true;
	if( wParam == SIZE_MAXIMIZED ){
		bMiniMapSizeBox = false;
	}
	nStatusBarHeight = 0;
	if( nullptr != m_cStatusBar.GetStatusHwnd() ){
		::SendMessage( m_cStatusBar.GetStatusHwnd(), WM_SIZE, wParam, lParam );
		::GetClientRect( m_cStatusBar.GetStatusHwnd(), &rc );
		//	May 12, 2000 genta
		//	2カラム目に改行コードの表示を挿入
		//	From Here
		// 2003.08.26 Moca CR0LF0廃止に従い、適当に調整
		// 2004-02-28 yasu 文字列を出力時の書式に合わせる
		// 幅を変えた場合にはCEditView::ShowCaretPosInfo()での表示方法を見直す必要あり．
		// ※pszLabel[3]: ステータスバー文字コード表示領域は大きめにとっておく
		const WCHAR* pszLabel[] = { L"", L"99999 行 9999 列", L"CRLF", L"AAAAAAAAAAAA", L"UTF-16 BOM付", L"REC", L"上書", L"9999 %" };	//Oct. 30, 2000 JEPRO 千万行も要らん	文字コード枠を広げる 2008/6/21	Uchi
		constexpr auto nStArrNum = std::size(pszLabel);
		int nStArr[std::size(pszLabel)] = {};
		//	To Here
		int			nAllWidth = rc.right - rc.left;
		int			nSbxWidth = ::GetSystemMetrics(SM_CXVSCROLL) + ::GetSystemMetrics(SM_CXEDGE); // サイズボックスの幅
		int			nBdrWidth = ::GetSystemMetrics(SM_CXSIZEFRAME) + ::GetSystemMetrics(SM_CXEDGE) * 2; // 境界の幅
		SIZE		sz;
		HDC			hdc;

		// 2004-02-28 yasu
		// 正確な幅を計算するために、表示フォントを取得してhdcに選択させる。
		hdc = ::GetDC( m_cStatusBar.GetStatusHwnd() );
		HFONT hFont = (HFONT)::SendMessage(m_cStatusBar.GetStatusHwnd(), WM_GETFONT, 0, 0);
		if (hFont != nullptr)
		{
			hFont = (HFONT)::SelectObject(hdc, hFont);
		}
		nStArr[nStArrNum - 1] = nAllWidth;
		if( wParam != SIZE_MAXIMIZED ){
			nStArr[nStArrNum - 1] -= nSbxWidth;
		}
		for (int i = nStArrNum - 1; i > 0; --i) {
			::GetTextExtentPoint32W(hdc, PSZ_ARGS(pszLabel[i]), &sz);
			nStArr[i - 1] = nStArr[i] - ( sz.cx + nBdrWidth );
		}

		//	Nov. 8, 2003 genta
		//	初期状態ではすべての部分が「枠あり」だが，メッセージエリアは枠を描画しないようにしている
		//	ため，初期化時の枠が変な風に残ってしまう．初期状態で枠を描画させなくするため，
		//	最初に「枠無し」状態を設定した後でバーの分割を行う．
		if( bUpdateStatus ){
			m_cStatusBar.SetStatusText(0, SBT_NOBORDERS, L"");
		}

		ApiWrap::StatusBar_SetParts( m_cStatusBar.GetStatusHwnd(), nStArrNum, nStArr );
		if (hFont != nullptr)
		{
			::SelectObject(hdc, hFont);
		}
		::ReleaseDC( m_cStatusBar.GetStatusHwnd(), hdc );

		::UpdateWindow( m_cStatusBar.GetStatusHwnd() );	// 2006.06.17 ryoji 即時描画でちらつきを減らす
		::GetWindowRect( m_cStatusBar.GetStatusHwnd(), &rc );
		nStatusBarHeight = rc.bottom - rc.top;
		bMiniMapSizeBox = false;
	}
	::GetClientRect( GetHwnd(), &rcClient );

	//@@@ From 2003.05.31 MIK
	//タブウインドウ追加に伴い，ファンクションキー表示位置も調整

	//タブウインドウ
	int nTabHeightBottom = 0;
	nTabWndHeight = 0;
	if( m_cTabWnd.GetHwnd() )
	{
		// タブ多段はSizeBox/ウィンドウ幅で高さが変わる可能性がある
		ETabPosition tabPosition = m_pShareData->m_Common.m_sTabBar.m_eTabPosition;
		bool bHidden = false;
		if( tabPosition == TabPosition_Top ){
			// 上から下に移動するとゴミが表示されるので一度非表示にする
			if( m_cTabWnd.m_eTabPosition != TabPosition_None && m_cTabWnd.m_eTabPosition != TabPosition_Top ){
				bHidden = true;
				::ShowWindow( m_cTabWnd.GetHwnd(), SW_HIDE );
			}
			m_cTabWnd.SizeBox_ONOFF( false );
			::GetWindowRect( m_cTabWnd.GetHwnd(), &rc );
			nTabWndHeight = rc.bottom - rc.top;
			if( m_pShareData->m_Common.m_sWindow.m_nFUNCKEYWND_Place == 0 ){
				::MoveWindow( m_cTabWnd.GetHwnd(), 0, nToolBarHeight + nFuncKeyWndHeight, cx, nTabWndHeight, TRUE );
			}else{
				::MoveWindow( m_cTabWnd.GetHwnd(), 0, nToolBarHeight, cx, nTabWndHeight, TRUE );
			}
			m_cTabWnd.OnSize();
			::GetWindowRect( m_cTabWnd.GetHwnd(), &rc );
			if( nTabWndHeight != rc.bottom - rc.top ){
				nTabWndHeight = rc.bottom - rc.top;
				if( m_pShareData->m_Common.m_sWindow.m_nFUNCKEYWND_Place == 0 ){
					::MoveWindow( m_cTabWnd.GetHwnd(), 0, nToolBarHeight + nFuncKeyWndHeight, cx, nTabWndHeight, TRUE );
				}else{
					::MoveWindow( m_cTabWnd.GetHwnd(), 0, nToolBarHeight, cx, nTabWndHeight, TRUE );
				}
			}
		}else if( tabPosition == TabPosition_Bottom ){
			// 上から下に移動するとゴミが表示されるので一度非表示にする
			if( m_cTabWnd.m_eTabPosition != TabPosition_None && m_cTabWnd.m_eTabPosition != TabPosition_Bottom ){
				bHidden = true;
				ShowWindow( m_cTabWnd.GetHwnd(), SW_HIDE );
			}
			bool	bSizeBox = true;
			if( nullptr != m_cStatusBar.GetStatusHwnd() ){
				bSizeBox = false;
			}
			if (1 == m_pShareData->m_Common.m_sWindow.m_nFUNCKEYWND_Place &&
				m_cFuncKeyWnd.GetHwnd()) {
					bSizeBox = false;
			}
			if( wParam == SIZE_MAXIMIZED ){
				bSizeBox = false;
			}
			m_cTabWnd.SizeBox_ONOFF( bSizeBox );
			::GetWindowRect( m_cTabWnd.GetHwnd(), &rc );
			nTabWndHeight = rc.bottom - rc.top;
			::MoveWindow( m_cTabWnd.GetHwnd(), 0,
				cy - nFuncKeyWndHeight - nStatusBarHeight - nTabWndHeight, cx, nTabWndHeight, TRUE );
			m_cTabWnd.OnSize();
			::GetWindowRect( m_cTabWnd.GetHwnd(), &rc );
			if( nTabWndHeight != rc.bottom - rc.top ){
				nTabWndHeight = rc.bottom - rc.top;
				::MoveWindow( m_cTabWnd.GetHwnd(), 0,
					cy - nFuncKeyWndHeight - nStatusBarHeight - nTabWndHeight, cx, nTabWndHeight, TRUE );
			}
			nTabHeightBottom = rc.bottom - rc.top;
			nTabWndHeight = 0;
			bMiniMapSizeBox = false;
		}
		if( bHidden ){
			::ShowWindow( m_cTabWnd.GetHwnd(), SW_SHOW );
		}
		m_cTabWnd.m_eTabPosition = tabPosition;
	}

	//	2005.04.23 genta ファンクションキー非表示の時は移動しない
	if( m_cFuncKeyWnd.GetHwnd() != nullptr ){
		if( m_pShareData->m_Common.m_sWindow.m_nFUNCKEYWND_Place == 0 )
		{	/* ファンクションキー表示位置／0:上 1:下 */
			::MoveWindow(
				m_cFuncKeyWnd.GetHwnd(),
				0,
				nToolBarHeight,
				cx,
				nFuncKeyWndHeight, TRUE );
		}
		else if( m_pShareData->m_Common.m_sWindow.m_nFUNCKEYWND_Place == 1 )
		{	/* ファンクションキー表示位置／0:上 1:下 */
			::MoveWindow(
				m_cFuncKeyWnd.GetHwnd(),
				0,
				cy - nFuncKeyWndHeight - nStatusBarHeight,
				cx,
				nFuncKeyWndHeight, TRUE
			);

			bool	bSizeBox = true;
			if( nullptr != m_cStatusBar.GetStatusHwnd() ){
				bSizeBox = false;
			}
			if( wParam == SIZE_MAXIMIZED ){
				bSizeBox = false;
			}
			m_cFuncKeyWnd.SizeBox_ONOFF( bSizeBox );
			bMiniMapSizeBox = false;
		}
		::UpdateWindow( m_cFuncKeyWnd.GetHwnd() );	// 2006.06.17 ryoji 即時描画でちらつきを減らす
	}

	int nFuncListWidth = 0;
	int nFuncListHeight = 0;
	if( m_cDlgFuncList.GetHwnd() && m_cDlgFuncList.IsDocking() )
	{
		::SendMessageAny( m_cDlgFuncList.GetHwnd(), WM_SIZE, wParam, lParam );
		::GetWindowRect( m_cDlgFuncList.GetHwnd(), &rc );
		nFuncListWidth = rc.right - rc.left;
		nFuncListHeight = rc.bottom - rc.top;
	}

	EDockSide eDockSideFL = m_cDlgFuncList.GetDockSide();
	int nTop = nToolBarHeight + nTabWndHeight;
	if( m_pShareData->m_Common.m_sWindow.m_nFUNCKEYWND_Place == 0)
		nTop += nFuncKeyWndHeight;
	int nHeight = cy - nToolBarHeight - nFuncKeyWndHeight - nTabWndHeight - nTabHeightBottom - nStatusBarHeight;
	if (m_hwndCurlSyntaxErrorList && m_bCurlSyntaxErrorListVisible) {
		const int panelHeight = std::min<int>(static_cast<int>(DpiScaleY(m_nCurlSyntaxErrorListHeight)), std::max<int>(0, nHeight / 2));
		const int summaryHeight = std::min<int>(static_cast<int>(DpiScaleY(22)), panelHeight);
		const int panelTop = nTop + nHeight - panelHeight;
		if (m_hwndCurlSyntaxErrorSummary) {
			::MoveWindow(m_hwndCurlSyntaxErrorSummary, 0, panelTop, cx, summaryHeight, TRUE);
		}
		::MoveWindow(m_hwndCurlSyntaxErrorList, 0, panelTop + summaryHeight, cx,
			std::max(0, panelHeight - summaryHeight), TRUE);
		nHeight -= panelHeight;
	}
	if( m_cDlgFuncList.GetHwnd() && m_cDlgFuncList.IsDocking() )
	{
		::MoveWindow(
			m_cDlgFuncList.GetHwnd(),
			(eDockSideFL == DOCKSIDE_RIGHT)? cx - nFuncListWidth: 0,
			(eDockSideFL == DOCKSIDE_BOTTOM)? nTop + nHeight - nFuncListHeight: nTop,
			(eDockSideFL == DOCKSIDE_LEFT || eDockSideFL == DOCKSIDE_RIGHT)? nFuncListWidth: cx,
			(eDockSideFL == DOCKSIDE_TOP || eDockSideFL == DOCKSIDE_BOTTOM)? nFuncListHeight: nHeight,
			TRUE
		);
		if( eDockSideFL == DOCKSIDE_RIGHT || eDockSideFL == DOCKSIDE_BOTTOM ){
			bMiniMapSizeBox = false;
		}
	}

	// ミニマップ
	int nMiniMapWidth = 0;
	if( m_cMiniMapView.GetHwnd() ){
		nMiniMapWidth = ::DpiScaleX(GetDllShareData().m_Common.m_sWindow.m_nMiniMapWidth);
		::MoveWindow( m_cMiniMapView.GetHwnd(),
			(eDockSideFL == DOCKSIDE_RIGHT)? cx - nFuncListWidth - nMiniMapWidth: cx - nMiniMapWidth,
			(eDockSideFL == DOCKSIDE_TOP)? nTop + nFuncListHeight: nTop,
			nMiniMapWidth,
			(eDockSideFL == DOCKSIDE_TOP || eDockSideFL == DOCKSIDE_BOTTOM)? nHeight - nFuncListHeight: nHeight,
			TRUE
		);
		m_cMiniMapView.SplitBoxOnOff( FALSE, FALSE, bMiniMapSizeBox );
	}

	::MoveWindow(
		m_cSplitterWnd.GetHwnd(),
		(eDockSideFL == DOCKSIDE_LEFT)? nFuncListWidth: 0,
		(eDockSideFL == DOCKSIDE_TOP)? nTop + nFuncListHeight: nTop,	//@@@ 2003.05.31 MIK
		((eDockSideFL == DOCKSIDE_LEFT || eDockSideFL == DOCKSIDE_RIGHT)? cx - nFuncListWidth: cx) - nMiniMapWidth,
		(eDockSideFL == DOCKSIDE_TOP || eDockSideFL == DOCKSIDE_BOTTOM)? nHeight - nFuncListHeight: nHeight,	//@@@ 2003.05.31 MIK
		TRUE
	);
	UpdateCSVTableLayout();
	UpdateRealtimeFilterLayout();
	//@@@ To 2003.05.31 MIK

	/* 印刷プレビューモードか */
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
	if( !m_pPrintPreview ){
		return 0L;
	}
	return m_pPrintPreview->OnSize(wParam, lParam);
}

/* WM_PAINT 描画処理 */
LRESULT CEditWnd::OnPaint(
	HWND			hwnd,	// handle of window
	UINT			uMsg,	// message identifier
	WPARAM			wParam,	// first message parameter
	LPARAM			lParam 	// second message parameter
)
{
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
	/* 印刷プレビューモードか */
	if( !m_pPrintPreview ){
		PAINTSTRUCT		ps;
		::BeginPaint( hwnd, &ps );
		::EndPaint( hwnd, &ps );
		return 0L;
	}
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
	return m_pPrintPreview->OnPaint(hwnd, uMsg, wParam, lParam);
}

/* 印刷プレビュー 垂直スクロールバーメッセージ処理 WM_VSCROLL */
LRESULT CEditWnd::OnVScroll( WPARAM wParam, LPARAM lParam )
{
	/* 印刷プレビューモードか */
	if( !m_pPrintPreview ){
		return 0;
	}
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
	return m_pPrintPreview->OnVScroll(wParam, lParam);
}

/* 印刷プレビュー 水平スクロールバーメッセージ処理 */
LRESULT CEditWnd::OnHScroll( WPARAM wParam, LPARAM lParam )
{
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
	/* 印刷プレビューモードか */
	if( !m_pPrintPreview ){
		return 0;
	}
	return m_pPrintPreview->OnHScroll( wParam, lParam );
}

LRESULT CEditWnd::OnLButtonDown( [[maybe_unused]] WPARAM wParam, LPARAM lParam )
{
	//by 鬼(2) キャプチャして押されたら非クライアントでもこっちに来る
	if(m_IconClicked != icNone)
		return 0;

	m_ptDragPosOrg.x = LOWORD(lParam);	// horizontal position of cursor
	m_ptDragPosOrg.y = HIWORD(lParam);	// vertical position of cursor
	m_bDragMode      = true;
	SetCapture( GetHwnd() );

	return 0;
}

LRESULT CEditWnd::OnLButtonUp( [[maybe_unused]] WPARAM wParam, [[maybe_unused]] LPARAM lParam )
{
	//by 鬼 2002/04/18
	if(m_IconClicked != icNone)
	{
		if(m_IconClicked == icDown)
		{
			m_IconClicked = icClicked;
			//by 鬼(2) タイマー(IDは適当です)
			SetTimer(GetHwnd(), IDT_SYSMENU, GetDoubleClickTime(), nullptr);
		}
		return 0;
	}

	m_bDragMode = false;
//	MYTRACE( L"m_bDragMode = FALSE (OnLButtonUp)\n");
	ReleaseCapture();
	::InvalidateRect( GetHwnd(), nullptr, TRUE );
	return 0;
}

/*!	WM_MOUSEMOVE処理
	@date 2008.05.05 novice メモリリーク修正
*/
LRESULT CEditWnd::OnMouseMove( WPARAM wParam, LPARAM lParam )
{
	//by 鬼
	if(m_IconClicked != icNone)
	{
		//by 鬼(2) 一回押された時だけ
		if(m_IconClicked == icDown)
		{
			POINT pt{};
			::GetCursorPos(&pt); //スクリーン座標

			if (HTSYSMENU == ::SendMessageW(GetHwnd(), WM_NCHITTEST, 0, pt.x | (pt.y << 16))) return 0L;

			::ReleaseCapture();

			m_IconClicked = icNone;

			if (cxx::com_pointer<IDataObject> pDataObject; SUCCEEDED(GetDocument()->GetDataObject(&pDataObject))) {
				CDropSource drop(true);

				//移動禁止なので、戻り値を見ない
				drop.DoDragDrop(pDataObject, DROPEFFECT_COPY | DROPEFFECT_LINK);
			}
		}
		return 0;
	}

//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
	if (!m_pPrintPreview){
		return 0;
	}
	else {
		return m_pPrintPreview->OnMouseMove( wParam, lParam );
	}
}

LRESULT CEditWnd::OnMouseWheel( WPARAM wParam, LPARAM lParam )
{
	if( m_pPrintPreview ){
		return m_pPrintPreview->OnMouseWheel( wParam, lParam );
	}
	return Views_DispatchEvent( GetHwnd(), WM_MOUSEWHEEL, wParam, lParam );
}

/** マウスホイール処理

	@date 2007.10.16 ryoji OnMouseWheel()から処理抜き出し
*/
BOOL CEditWnd::DoMouseWheel( WPARAM wParam, LPARAM lParam )
{
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
	/* 印刷プレビューモードか */
	if( !m_pPrintPreview ){
		// 2006.03.26 ryoji by assitance with John タブ上ならウィンドウ切り替え
		if( m_pShareData->m_Common.m_sTabBar.m_bChgWndByWheel && nullptr != m_cTabWnd.m_hwndTab )
		{
			POINT pt;
			pt.x = (short)LOWORD( lParam );
			pt.y = (short)HIWORD( lParam );
			int nDelta = (short)HIWORD( wParam );
			HWND hwnd = ::WindowFromPoint( pt );
			if( (hwnd == m_cTabWnd.m_hwndTab || hwnd == m_cTabWnd.GetHwnd()) )
			{
				// 現在開いている編集窓のリストを得る
				EditNode* pEditNodeArr;
				int nRowNum = CAppNodeManager::getInstance()->GetOpenedWindowArr( &pEditNodeArr, TRUE );
				if(  nRowNum > 0 )
				{
					// 自分のウィンドウを調べる
					int i, j;
					int nGroup = 0;
					for( i = 0; i < nRowNum; ++i )
					{
						if( GetHwnd() == pEditNodeArr[i].GetHwnd() )
						{
							nGroup = pEditNodeArr[i].m_nGroup;
							break;
						}
					}
					if( i < nRowNum )
					{
						if( nDelta < 0 )
						{
							// 次のウィンドウ
							for( j = i + 1; j < nRowNum; ++j )
							{
								if( nGroup == pEditNodeArr[j].m_nGroup )
									break;
							}
							if( j >= nRowNum )
							{
								for( j = 0; j < i; ++j )
								{
									if( nGroup == pEditNodeArr[j].m_nGroup )
										break;
								}
							}
						}
						else
						{
							// 前のウィンドウ
							for( j = i - 1; j >= 0; --j )
							{
								if( nGroup == pEditNodeArr[j].m_nGroup )
									break;
							}
							if( j < 0 )
							{
								for( j = nRowNum - 1; j > i; --j )
								{
									if( nGroup == pEditNodeArr[j].m_nGroup )
										break;
								}
							}
						}

						/* 次の（or 前の）ウィンドウをアクティブにする */
						if( i != j )
							ActivateFrameWindow( pEditNodeArr[j].GetHwnd() );
					}

					delete []pEditNodeArr;
				}
				return TRUE;	// 処理した
			}
		}
		return FALSE;	// 処理しなかった
	}
	return FALSE;	// 処理しなかった
}

/* 印刷ページ設定
	印刷プレビュー時にも、そうでないときでも呼ばれる可能性がある。
*/
BOOL CEditWnd::OnPrintPageSetting( void )
{
	/* 印刷設定（CANCEL押したときに破棄するための領域） */
	CDlgPrintSetting	cDlgPrintSetting;
	BOOL				bRes;
	int					nCurrentPrintSetting;
	int					nLineNumberColumns;

	nCurrentPrintSetting = GetDocument()->m_cDocType.GetDocumentAttribute().m_nCurrentPrintSetting;
	if( m_pPrintPreview ){
		nLineNumberColumns = GetActiveView().GetTextArea().DetectWidthOfLineNumberArea_calculate(m_pPrintPreview->m_pLayoutMgr_Print); // 印刷プレビュー時は文書の桁数 2013.5.10 aroka
	}else{
		nLineNumberColumns = 3; // ファイルメニューからの設定時は最小値 2013.5.10 aroka
	}

	bRes = cDlgPrintSetting.DoModal(
		G_AppInstance(),
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
		GetHwnd(),
		&nCurrentPrintSetting, /* 現在選択している印刷設定 */
		m_pShareData->m_PrintSettingArr, // 現在の設定はダイアログ側で保持する 2013.5.1 aroka
		nLineNumberColumns // 行番号表示用に桁数を渡す 2013.5.10 aroka
	);

	if( FALSE != bRes ){
		bool bChangePrintSettingNo = false;
		/* 現在選択されているページ設定の番号が変更されたか */
		if( GetDocument()->m_cDocType.GetDocumentAttribute().m_nCurrentPrintSetting != nCurrentPrintSetting )
		{
			/* 変更フラグ(タイプ別設定) */
			STypeConfig* type = new STypeConfig();
			CDocTypeManager().GetTypeConfig( GetDocument()->m_cDocType.GetDocumentType(), *type );
			type->m_nCurrentPrintSetting = nCurrentPrintSetting;
			CDocTypeManager().SetTypeConfig( GetDocument()->m_cDocType.GetDocumentType(), *type );
			delete type;
			GetDocument()->m_cDocType.GetDocumentAttributeWrite().m_nCurrentPrintSetting = nCurrentPrintSetting; // 今の設定にも反映
			CAppNodeGroupHandle(0).SendMessageToAllEditors(
				MYWM_CHANGESETTING,
				(WPARAM)GetDocument()->m_cDocType.GetDocumentType().GetIndex(),
				(LPARAM)PM_CHANGESETTING_TYPE,
				CEditWnd::getInstance()->GetHwnd()
			);
			bChangePrintSettingNo = true;
		}

//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
		//	印刷プレビュー時のみ。
		if ( m_pPrintPreview ){
			/* 現在の印刷設定 */
			// 2013.08.27 印刷設定番号が変更された時に対応できていなかった
			if( bChangePrintSettingNo ){
				m_pPrintPreview->SetPrintSetting( &m_pShareData->m_PrintSettingArr[GetDocument()->m_cDocType.GetDocumentAttribute().m_nCurrentPrintSetting] );
			}

			/* 印刷プレビュー スクロールバー初期化 */
			//m_pPrintPreview->InitPreviewScrollBar();

			/* 印刷設定の反映 */
			// m_pPrintPreview->OnChangePrintSetting( );

			//::InvalidateRect( GetHwnd(), NULL, TRUE );
		}
		CAppNodeGroupHandle(0).SendMessageToAllEditors(
			MYWM_CHANGESETTING,
			(WPARAM)0,
			(LPARAM)PM_PRINTSETTING,
			CEditWnd::getInstance()->GetHwnd()
		);
	}
//@@@ 2002.01.14 YAZAKI 印刷プレビューをCPrintPreviewに独立させたことによる変更
	::UpdateWindow( GetHwnd() /* m_pPrintPreview->GetPrintPreviewBarHANDLE() */);
	return bRes;
}

///////////////////////////// by 鬼

LRESULT CEditWnd::OnNcLButtonDown(WPARAM wp, LPARAM lp)
{
	LRESULT Result;
	if(wp == HTSYSMENU)
	{
		SetCapture(GetHwnd());
		m_IconClicked = icDown;
		Result = 0;
	}
	else
		Result = DefWindowProc(GetHwnd(), WM_NCLBUTTONDOWN, wp, lp);

	return Result;
}

LRESULT CEditWnd::OnNcLButtonUp(WPARAM wp, LPARAM lp)
{
	LRESULT Result;
	if(m_IconClicked != icNone)
	{
		//念のため
		ReleaseCapture();
		m_IconClicked = icNone;
		Result = 0;
	}
	else if(wp == HTSYSMENU)
		Result = 0;
	else{
		//	2004.05.23 Moca メッセージミス修正
		//	フレームのダブルクリック時後にウィンドウサイズ
		//	変更モードなっていた
		Result = DefWindowProc(GetHwnd(), WM_NCLBUTTONUP, wp, lp);
	}

	return Result;
}

LRESULT CEditWnd::OnLButtonDblClk(WPARAM wp, LPARAM lp) //by 鬼(2)
{
	LRESULT Result;
	if(m_IconClicked != icNone)
	{
		ReleaseCapture();
		m_IconClicked = icDoubleClicked;

		SendMessage(GetHwnd(), WM_SYSCOMMAND, SC_CLOSE, 0);

		Result = 0;
	}
	else {
		//	2004.05.23 Moca メッセージミス修正
		Result = DefWindowProc(GetHwnd(), WM_LBUTTONDBLCLK, wp, lp);
	}

	return Result;
}

/*! ドロップダウンメニュー(開く) */	//@@@ 2002.06.15 MIK
int	CEditWnd::CreateFileDropDownMenu( HWND hwnd )
{
	int			nId;
	HMENU		hMenu;
	HMENU		hMenuPopUp;
	POINT		po;
	RECT		rc;
	int			nIndex;

	// メニュー表示位置を決める	// 2007.03.25 ryoji
	// ※ TBN_DROPDOWN 時の NMTOOLBAR::iItem や NMTOOLBAR::rcButton にはドロップダウンメニュー(開く)ボタンが
	//    複数あるときはどれを押した時も１個目のボタン情報が入るようなのでマウス位置からボタン位置を求める
	::GetCursorPos( &po );
	::ScreenToClient( hwnd, &po );
	nIndex = ApiWrap::Toolbar_Hittest( hwnd, &po );
	if( nIndex < 0 ){
		return 0;
	}
	ApiWrap::Toolbar_GetItemRect( hwnd, nIndex, &rc );
	po.x = rc.left;
	po.y = rc.bottom;
	::ClientToScreen( hwnd, &po );
	GetMonitorWorkRect( po, &rc );
	if( po.x < rc.left )
		po.x = rc.left;
	if( po.y < rc.top )
		po.y = rc.top;

	m_cMenuDrawer.ResetContents();

	/* 空メニューを作る */
	hMenu = ::CreatePopupMenu();

	/* MRUリストのファイルのリストをメニューにする */
	const CMRUFile cMRU;
	hMenu = cMRU.CreateMenu( hMenu, &m_cMenuDrawer );
	if( cMRU.MenuLength() > 0 )
	{
		m_cMenuDrawer.MyAppendMenuSep( hMenu, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr, FALSE );
	}

	/* 最近使ったフォルダーのメニューを作成 */
	const CMRUFolder cMRUFolder;
	hMenuPopUp = cMRUFolder.CreateMenu( &m_cMenuDrawer );
	if ( cMRUFolder.MenuLength() > 0 )
	{
		//	アクティブ
		m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING | MF_POPUP, (UINT_PTR)hMenuPopUp, LS(F_FOLDER_USED_RECENTLY), L"" );
	}
	else
	{
		//	非アクティブ
		m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING | MF_POPUP | MF_GRAYED, (UINT_PTR)hMenuPopUp, LS(F_FOLDER_USED_RECENTLY), L"" );
	}

	m_cMenuDrawer.MyAppendMenuSep( hMenu, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr, FALSE );

	/* 履歴の管理のメニューを作成 */
	m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING, F_FAVORITE, L"", L"M", FALSE );
	m_cMenuDrawer.MyAppendMenuSep( hMenu, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr, FALSE );

	m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING, F_FILENEW, L"", L"N", FALSE );
	m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING, F_FILENEW_NEWWINDOW, L"", L"M", FALSE );
	m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING, F_FILEOPEN, L"", L"O", FALSE );

	nId = ::TrackPopupMenu(
		hMenu,
		TPM_TOPALIGN
		| TPM_LEFTALIGN
		| TPM_RETURNCMD
		| TPM_LEFTBUTTON
		,
		po.x,
		po.y,
		0,
		GetHwnd(),	// 2009.02.03 ryoji アクセスキー有効化のため hwnd -> GetHwnd() に変更
		nullptr
	);

	::DestroyMenu( hMenu );

	return nId;
}

/*!
	@brief ウィンドウのアイコン設定

	指定されたアイコンをウィンドウに設定する．
	以前のアイコンは破棄する．

	@param hIcon [in] 設定するアイコン
	@param flag [in] アイコン種別．ICON_BIGまたはICON_SMALL.
	@author genta
	@date 2002.09.10
*/
void CEditWnd::SetWindowIcon(HICON hIcon, int flag) const
{
	if (const auto hOld = (HICON)::SendMessageW(GetHwnd(), WM_SETICON, flag, LPARAM(hIcon));
		hOld != nullptr ){
		::DestroyIcon( hOld );
	}
}

/*!
	標準アイコンの取得

	@param hIconBig   [out] 大きいアイコンのハンドル
	@param hIconSmall [out] 小さいアイコンのハンドル

	@author genta
	@date 2002.09.10
	@date 2002.12.02 genta 新設した共通関数を使うように
*/
void CEditWnd::GetDefaultIcon( HICON* hIconBig, HICON* hIconSmall ) const
{
	*hIconBig   = GetAppIcon( G_AppInstance(), ICON_DEFAULT_APP, FN_APP_ICON, false );
	*hIconSmall = GetAppIcon( G_AppInstance(), ICON_DEFAULT_APP, FN_APP_ICON, true );
}

/*!
	アイコンの取得

	指定されたファイル名に対応するアイコン(大・小)を取得して返す．

	@param szFile     [in] ファイル名
	@param hIconBig   [out] 大きいアイコンのハンドル
	@param hIconSmall [out] 小さいアイコンのハンドル

	@retval true 関連づけられたアイコンが見つかった
	@retval false 関連づけられたアイコンが見つからなかった

	@author genta
	@date 2002.09.10
*/
bool CEditWnd::GetRelatedIcon(const WCHAR* szFile, HICON* hIconBig, HICON* hIconSmall) const
{
	if( nullptr != szFile && szFile[0] != L'\0' ){
		WCHAR szExt[_MAX_EXT];
		WCHAR FileType[1024];

		// (.で始まる)拡張子の取得
		_wsplitpath_s( szFile, nullptr, 0, nullptr, 0, nullptr, 0, szExt, std::size(szExt) );

		if( ReadRegistry(HKEY_CLASSES_ROOT, szExt, nullptr, FileType, int(std::size(FileType)) - 13)){
			wcscat( FileType, L"\\DefaultIcon" );
			if( ReadRegistry(HKEY_CLASSES_ROOT, FileType, nullptr, nullptr, 0)){
				// 関連づけられたアイコンを取得する
				SHFILEINFO shfi;
				SHGetFileInfo( szFile, 0, &shfi, sizeof(shfi), SHGFI_ICON | SHGFI_LARGEICON );
				*hIconBig = shfi.hIcon;
				SHGetFileInfo( szFile, 0, &shfi, sizeof(shfi), SHGFI_ICON | SHGFI_SMALLICON );
				*hIconSmall = shfi.hIcon;
				return true;
			}
		}
	}

	//	標準のアイコンを返す
	GetDefaultIcon( hIconBig, hIconSmall );
	return false;
}

/*
	@brief メニューバー表示用フォントの初期化

	メニューバー表示用フォントの初期化を行う．

	@date 2002.12.04 CEditViewのコンストラクタから移動
*/
void CEditWnd::InitMenubarMessageFont(void)
{
	TEXTMETRIC	tm;
	LOGFONT		lf;

	/* LOGFONTの初期化 */
	memset_raw( &lf, 0, sizeof( lf ) );
	lf.lfHeight			= DpiPointsToPixels(-9);	// 2009.10.01 ryoji 高DPI対応（ポイント数から算出）
	lf.lfWidth			= 0;
	lf.lfEscapement		= 0;
	lf.lfOrientation	= 0;
	lf.lfWeight			= 400;
	lf.lfItalic			= 0x0;
	lf.lfUnderline		= 0x0;
	lf.lfStrikeOut		= 0x0;
	lf.lfCharSet		= 0x80;
	lf.lfOutPrecision	= 0x3;
	lf.lfClipPrecision	= 0x2;
	lf.lfQuality		= 0x1;
	lf.lfPitchAndFamily	= 0x31;
	wcscpy( lf.lfFaceName, L"ＭＳ ゴシック" );
	m_hFontCaretPosInfo = ::CreateFontIndirect( &lf );

	MemDcHolder hdc = ::CreateCompatibleDC(nullptr);
	SelectionHolder hFontOld{ hdc };
	hFontOld = ::SelectObject( hdc, m_hFontCaretPosInfo );
	::GetTextMetrics( hdc, &tm );
	m_nCaretPosInfoCharWidth = tm.tmAveCharWidth;
	m_nCaretPosInfoCharHeight = tm.tmHeight;
}

/*
	@brief メニューバーにメッセージを表示する

	事前にメニューバー表示用フォントが初期化されていなくてはならない．
	指定できる文字数は最大30文字．それ以上の場合はうち切って表示する．

	@author genta
	@date 2002.12.04
*/
void CEditWnd::PrintMenubarMessage( const WCHAR* msg )
{
	const auto hWnd = GetHwnd();

	if( nullptr == ::GetMenu( GetHwnd() ) )	// 2007.03.08 ryoji 追加
		return;

	POINT	po,poFrame;
	RECT	rc,rcFrame;
	int		nStrLen;

	// msg == NULL のときは以前の m_pszMenubarMessage で再描画
	if( msg ){
		auto len = int(wcslen(msg));
		wcsncpy( m_pszMenubarMessage, msg, MENUBAR_MESSAGE_MAX_LEN );
		if( len < MENUBAR_MESSAGE_MAX_LEN ){
			wmemset( m_pszMenubarMessage + len, L' ', MENUBAR_MESSAGE_MAX_LEN - len );	//  null終端は不要
		}
	}

	WindowDcHolder hdc{ hWnd };
	hdc = ::GetWindowDC(hWnd);
	SelectionHolder hFontOld{ hdc };
	poFrame.x = 0;
	poFrame.y = 0;
	::ClientToScreen( GetHwnd(), &poFrame );
	::GetWindowRect( GetHwnd(), &rcFrame );
	po.x = rcFrame.right - rcFrame.left;
	po.y = poFrame.y - rcFrame.top;
	hFontOld = ::SelectObject( hdc, m_hFontCaretPosInfo );
	nStrLen = MENUBAR_MESSAGE_MAX_LEN;
	rc.left = po.x - nStrLen * m_nCaretPosInfoCharWidth - ( ::GetSystemMetrics( SM_CXSIZEFRAME ) + 2 );
	rc.right = rc.left + nStrLen * m_nCaretPosInfoCharWidth + 2;
	rc.top = po.y - m_nCaretPosInfoCharHeight - 2;
	rc.bottom = rc.top + m_nCaretPosInfoCharHeight;
	::SetTextColor( hdc, DarkMode::getTextColor() );
	::SetBkColor( hdc, DarkMode::getDlgBackgroundColor());
	{
		const WCHAR* pchText = m_pszMenubarMessage;
		const ULONG cchText = nStrLen;
		const INT nMaxExtent = rc.right - rc.left;
		const DWORD dwFlags = ::GetFontLanguageInfo(hdc);
		INT vDx[MENUBAR_MESSAGE_MAX_LEN] = { 0 };
		WCHAR vGlyphs[(MENUBAR_MESSAGE_MAX_LEN * 3 / 2) + 16]; // エラーグリフの増分を加味した領域を確保

		GCP_RESULTS results = { sizeof(GCP_RESULTS) };
		results.lpDx = vDx;
		results.lpGlyphs = vGlyphs;
		results.nGlyphs = int(std::size(vGlyphs));
		results.nMaxFit = cchText;
		auto placement = ::GetCharacterPlacement(hdc, pchText, cchText, nMaxExtent, &results, dwFlags);

		if (placement != 0) {
			::ExtTextOut(hdc, rc.left, rc.top, ETO_CLIPPED | ETO_OPAQUE, &rc, m_pszMenubarMessage, nStrLen, vDx);
		}
	}
}

/*!
	@brief メッセージの表示

	指定されたメッセージをステータスバーに表示する．
	ステータスバーが非表示の場合はメニューバーの右端に表示する．

	@param msg [in] 表示するメッセージ
	@date 2002.01.26 hor 新規作成
	@date 2002.12.04 genta CEditViewより移動
*/
void CEditWnd::SendStatusMessage( const WCHAR* msg )
{
	if( nullptr == m_cStatusBar.GetStatusHwnd() ){
		// メニューバーへ
		PrintMenubarMessage( msg );
	}
	else{
		// ステータスバーへ
		m_cStatusBar.SetStatusText(0, SBT_NOBORDERS, msg);
	}
}

/*! ファイル名変更通知

	@author MIK
	@date 2003.05.31 新規作成
	@date 2006.01.28 ryoji ファイル名、Grepモードパラメータを追加
*/
void CEditWnd::ChangeFileNameNotify(
	std::wstring_view tabCaption,
	std::wstring_view tabFilePath,
	bool bIsGrep
) const
{
	const auto pszTabCaption = std::data(tabCaption);
	const auto pszFilePath = std::data(tabFilePath);

	CRecentEditNode	cRecentEditNode;
	int nIndex = cRecentEditNode.FindItemByHwnd( GetHwnd() );
	bool changed = false;
	if( -1 != nIndex )
	{
		EditNode *p = cRecentEditNode.GetItem( nIndex );
		if( p )
		{
			decltype(p->m_szTabCaption) caption;
			wcsncpy_s(caption, std::size(caption), pszTabCaption, _TRUNCATE);
			if (wcscmp(caption, p->m_szTabCaption) != 0) {
				wcscpy_s(p->m_szTabCaption, caption);
				changed = true;
			}

			// 2006.01.28 ryoji ファイル名、Grepモード追加
			decltype(p->m_szFilePath) filePath;
			wcsncpy_s(filePath, std::size(filePath), pszFilePath, _TRUNCATE );
			if (wcscmp(filePath, p->m_szFilePath) != 0) {
				p->m_szFilePath = filePath;
				changed = true;
			}

			p->m_bIsGrep = bIsGrep;
		}
	}
	cRecentEditNode.Terminate();

	if (changed) {
		//ファイル名変更通知をブロードキャストする。
		int nGroup = CAppNodeManager::getInstance()->GetEditNode( GetHwnd() )->GetGroup();
		CAppNodeGroupHandle(nGroup).PostMessageToAllEditors(
			MYWM_TAB_WINDOW_NOTIFY,
			(WPARAM)TWNT_FILE,
			(LPARAM)GetHwnd(),
			GetHwnd()
		);
	}

	return;
}

/*! 常に手前に表示
	@param top  0:トグル動作 1:最前面 2:最前面解除 その他:なにもしない
	@date 2004.09.21 Moca
*/
void CEditWnd::WindowTopMost(int top) const
{
	if( 0 == top ){
		DWORD dwExstyle = (DWORD)::GetWindowLongPtr( GetHwnd(), GWL_EXSTYLE );
		if( dwExstyle & WS_EX_TOPMOST ){
			top = 2; // 最前面である -> 解除
		}else{
			top = 1;
		}
	}

	HWND hwndInsertAfter;
	switch( top ){
	case 1:
		hwndInsertAfter = HWND_TOPMOST;
		break;
	case 2:
		hwndInsertAfter = HWND_NOTOPMOST;
		break;
	default:
		return;
	}

	::SetWindowPos( GetHwnd(), hwndInsertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );

	// タブまとめ時は WS_EX_TOPMOST 状態を全ウィンドウで同期する	// 2007.05.18 ryoji
	if( m_pShareData->m_Common.m_sTabBar.m_bDispTabWnd && !m_pShareData->m_Common.m_sTabBar.m_bDispTabWndMultiWin ){
		HWND hwnd;
		int i;
		for( i = 0, hwndInsertAfter = GetHwnd(); i < m_pShareData->m_sNodes.m_nEditArrNum; i++ ){
			hwnd = m_pShareData->m_sNodes.m_pEditArr[i].GetHwnd();
			if( hwnd != GetHwnd() && IsSakuraMainWindow( hwnd ) ){
				if( !CAppNodeManager::IsSameGroup( GetHwnd(), hwnd ) )
					continue;
				::SetWindowPos( hwnd, hwndInsertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
				hwndInsertAfter = hwnd;
			}
		}
	}
}

// タイマーの更新を開始／停止する。 20060128 aroka
// ツールバー表示はタイマーにより更新しているが、
// アプリのフォーカスが外れたときにウィンドウからON/OFFを
//	呼び出してもらうことにより、余計な負荷を停止したい。
void CEditWnd::Timer_ONOFF(bool bStart) const
{
	if( nullptr != GetHwnd() ){
		if( bStart ){
			/* タイマーを起動 */
			if( 0 == ::SetTimer( GetHwnd(), IDT_TOOLBAR, 300, nullptr ) ){
				WarningMessage( GetHwnd(), LS(STR_ERR_DLGEDITWND03) );
			}
		} else {
			/* タCマーを削除 */
			::KillTimer( GetHwnd(), IDT_TOOLBAR );
		}
	}
	return;
}

/*!	@brief ウィンドウ一覧をポップアップ表示

	@param[in] bMousePos true: マウス位置にポップアップ表示する

	@date 2006.03.23 fon OnListBtnClickをベースに新規作成
	@date 2006.05.10 ryoji ポップアップ位置変更、その他微修正
	@date 2007.02.28 ryoji フルパス指定のパラメータを削除
	@date 2009.06.02 ryoji m_cMenuDrawerの初期化漏れ修正
*/
LRESULT CEditWnd::PopupWinList( bool bMousePos )
{
	POINT pt;

	// ポップアップ位置をアクティブビューの上辺に設定
	RECT rc;

	if( bMousePos ){
		::GetCursorPos( &pt );	// マウスカーソル位置に変更
	}
	else {
		::GetWindowRect( GetActiveView().GetHwnd(), &rc );
		pt.x = rc.right - 150;
		if( pt.x < rc.left )
			pt.x = rc.left;
		pt.y = rc.top;
	}

	// ウィンドウ一覧メニューをポップアップ表示する
	if( nullptr != m_cTabWnd.GetHwnd() ){
		m_cTabWnd.TabListMenu( pt );
	}
	else{
		m_cMenuDrawer.ResetContents();	// 2009.06.02 ryoji 追加
		EditNode*	pEditNodeArr;
		HMENU hMenu = ::CreatePopupMenu();	// 2006.03.23 fon
		int nRowNum = CAppNodeManager::getInstance()->GetOpenedWindowArr( &pEditNodeArr, TRUE );
		WinListMenu( hMenu, pEditNodeArr, nRowNum, TRUE );
		// メニューを表示する
		RECT rcWork;
		GetMonitorWorkRect( pt, &rcWork );	// モニタのワークエリア
		int nId = ::TrackPopupMenu( hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD,
									( pt.x > rcWork.left )? pt.x: rcWork.left,
									( pt.y < rcWork.bottom )? pt.y: rcWork.bottom,
									0, GetHwnd(), nullptr);
		delete [] pEditNodeArr;
		::DestroyMenu( hMenu );
		::SendMessage( GetHwnd(), WM_COMMAND, (WPARAM)nId, (LPARAM)nullptr );
	}

	return 0L;
}

/*! @brief 現在開いている編集窓のリストをメニューにする
	@date  2006.03.23 fon CEditWnd::InitMenuから移動。////が元からあるコメント。//>は追加コメントアウト。
	@date 2009.06.02 ryoji アイテム数が多いときはアクセスキーを 1-9,A-Z の範囲で再使用する（従来は36個未満を仮定）
*/
LRESULT CEditWnd::WinListMenu( HMENU hMenu, EditNode* pEditNodeArr, int nRowNum, [[maybe_unused]] BOOL bFull )
{
	int			i;
	WCHAR		szMenu[_MAX_PATH * 2 + 3];
	const EditInfo*	pfi;

	if( nRowNum > 0 ){
		CFileNameManager::getInstance()->TransformFileName_MakeCache();

		NONCLIENTMETRICS met;
		met.cbSize = CCSIZEOF_STRUCT(NONCLIENTMETRICS, lfMessageFont);
		::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, met.cbSize, &met, 0);
		CDCFont dcFont(met.lfMenuFont, GetHwnd());
		for( i = 0; i < nRowNum; ++i ){
			/* トレイからエディタへの編集ファイル名要求通知 */
			::SendMessage( pEditNodeArr[i].GetHwnd(), MYWM_GETFILEINFO, 0, 0 );
////	From Here Oct. 4, 2000 JEPRO commented out & modified	開いているファイル数がわかるように履歴とは違って1から数える
			pfi = (EditInfo*)&m_pShareData->m_sWorkBuffer.m_EditInfo_MYWM_GETFILEINFO;
			CFileNameManager::getInstance()->GetMenuFullLabel_WinList( szMenu, int(std::size(szMenu)), pfi, pEditNodeArr[i].m_nId, i, dcFont.GetHDC() );
			m_cMenuDrawer.MyAppendMenu( hMenu, MF_BYPOSITION | MF_STRING, IDM_SELWINDOW + pEditNodeArr[i].m_nIndex, szMenu, L"" );
			if( GetHwnd() == pEditNodeArr[i].GetHwnd() ){
				::CheckMenuItem( hMenu, IDM_SELWINDOW + pEditNodeArr[i].m_nIndex, MF_BYCOMMAND | MF_CHECKED );
			}
		}
	}
	return 0L;
}

//2007.09.08 kobake 追加
//!ツールチップのテキストを取得
void CEditWnd::GetTooltipText(WCHAR* pszBuf, size_t nBufCount, UINT_PTR idFrom) const
{
	const auto nID = int(idFrom);

	// 機能文字列の取得 -> pszBuf
	GetDocument()->m_cFuncLookup.Funccode2Name( nID, pszBuf, nBufCount );
	size_t nLen = wcsnlen( pszBuf, nBufCount );

	// 機能に対応するキー名の取得(複数)
	CNativeW**	ppcAssignedKeyList;
	int nAssignedKeyNum = CKeyBind::GetKeyStrList(
		G_AppInstance(),
		m_pShareData->m_Common.m_sKeyBind.m_nKeyNameArrNum,
		m_pShareData->m_Common.m_sKeyBind.m_pKeyNameArr,
		&ppcAssignedKeyList,
		nID
	);

	// pszBufへ結合
	if( 0 < nAssignedKeyNum ){
		for( int j = 0; j < nAssignedKeyNum; ++j ){
			const WCHAR* pszKey = ppcAssignedKeyList[j]->GetStringPtr();
			auto nKeyLen = int(wcslen(pszKey));
			if ( nLen + 9 + nKeyLen < nBufCount ){
				wcscat_s( pszBuf, nBufCount, L"\n        " );
				wcscat_s( pszBuf, nBufCount, pszKey );
				nLen += 9 + nKeyLen;
			}
			delete ppcAssignedKeyList[j];
		}
		delete [] ppcAssignedKeyList;
	}
}

/*! タイマーの処理
	@date 2002.01.03 YAZAKI m_tbMyButtonなどをCShareDataからCMenuDrawerへ移動したことによる修正。
	@date 2003.08.29 wmlhq, ryoji nTimerCountの導入
	@date 2006.01.28 aroka ツールバー更新を OnToolbarTimerに移動した
	@date 2007.04.03 ryoji パラメータ無しにした
*/
void CEditWnd::OnEditTimer( void )
{
	//static	int	nLoopCount = 0; // wmlhq m_nTimerCountに移行
	// タイマーの呼び出し間隔を 500msに変更。300*10→500*6にする。 20060128 aroka
	IncrementTimerCount(6);

	// 2006.01.28 aroka ツールバー更新関連は OnToolbarTimerに移動した。

	//	Aug. 29, 2003 wmlhq, ryoji
	if( m_nTimerCount == 0 && GetCapture() == nullptr ){
		// ファイルのタイムスタンプのチェック処理
		GetDocument()->m_cAutoReloadAgent.CheckFileTimeStamp();

#if 0	// 2011.02.11 ryoji 書込禁止の監視を廃止（復活させるなら「更新の監視」付随ではなく別オプションにしてほしい）
		// ファイル書込可能のチェック処理
		if(GetDocument()->m_cAutoReloadAgent._ToDoChecking()){
			bool bOld = GetDocument()->m_cDocLocker.IsDocWritable();
			GetDocument()->m_cDocLocker.CheckWritable(false);
			if(bOld != GetDocument()->m_cDocLocker.IsDocWritable()){
				this->UpdateCaption();
			}
		}
#endif
	}

	GetDocument()->m_cAutoSaveAgent.CheckAutoSave();
}

// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //
//                        ビュー管理                           //
// -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- //

/*!
	CEditViewの画面バッファを削除
	@date 2007.09.09 Moca 新規作成
*/
void CEditWnd::Views_DeleteCompatibleBitmap()
{
	// CEditView群へ転送する
	for( int i = 0; i < GetAllViewCount(); i++ ){
		if( GetView(i).GetHwnd() ){
			GetView(i).DeleteCompatibleBitmap();
		}
	}
	m_cMiniMapView.DeleteCompatibleBitmap();
}

LRESULT CEditWnd::Views_DispatchEvent(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch( msg ){
	case WM_ENTERMENULOOP:
	case WM_EXITMENULOOP:
		for( int i = 0; i < GetAllViewCount(); i++){
			GetView(i).DispatchEvent( hwnd, msg, wParam, lParam );
		}
		return 0L;
	default:
		return GetActiveView().DispatchEvent( hwnd, msg, wParam, lParam );
	}
}

/*
	分割指示。2つ目以降のビューを作る
	@param nViewCount  既存のビューも含めたビューの合計要求数
*/
bool CEditWnd::CreateEditViewBySplit(int nViewCount )
{
	if( m_nEditViewMaxCount < nViewCount ){
		return false;
	}
	if( GetAllViewCount() < nViewCount ){
		for( int i = GetAllViewCount(); i < nViewCount; i++ ){
			assert( nullptr == m_pcEditViewArr[i] );
			m_pcEditViewArr[i] = std::make_unique<CEditView>();
			m_pcEditViewArr[i]->Create( m_cSplitterWnd.GetHwnd(), GetDocument(), i, FALSE, false );
		}
		m_nEditViewCount = nViewCount;

		std::vector<HWND> hWndArr;
		hWndArr.reserve(nViewCount + 1);
		for( int i = 0; i < nViewCount; i++ ){
			hWndArr.push_back( GetView(i).GetHwnd() );
		}
		hWndArr.push_back( nullptr );

		m_cSplitterWnd.SetChildWndArr( &hWndArr[0] );
	}
	return true;
}

/*
	ビューの再初期化
	@date 2010.04.10 CEditDoc::InitAllViewから移動
*/
void CEditWnd::InitAllViews()
{
	/* 先頭へカーソルを移動 */
	for( int i = 0; i < GetAllViewCount(); ++i ){
		//	Apr. 1, 2001 genta
		// 移動履歴の消去
		GetView(i).m_cHistory->Flush();

		/* 現在の選択範囲を非選択状態に戻す */
		GetView(i).GetSelectionInfo().DisableSelectArea( false );

		GetView(i).OnChangeSetting();
		GetView(i).GetCaret().MoveCursor( CLayoutPoint(0, 0), true );
		GetView(i).GetCaret().m_nCaretPosX_Prev = CLayoutInt(0);
	}
	m_cMiniMapView.OnChangeSetting();
}

void CEditWnd::Views_RedrawAll()
{
	//アクティブ以外を再描画してから…
	for( int v = 0; v < GetAllViewCount(); ++v ){
		if( m_nActivePaneIndex != v ){
			GetView(v).RedrawAll();
		}
	}
	m_cMiniMapView.RedrawAll();
	//アクティブを再描画
	GetActiveView().RedrawAll();
}

void CEditWnd::Views_Redraw()
{
	//アクティブ以外を再描画してから…
	for( int v = 0; v < GetAllViewCount(); ++v ){
		if( m_nActivePaneIndex != v )
			GetView(v).Redraw();
	}
	m_cMiniMapView.Redraw();
	//アクティブを再描画
	GetActiveView().Redraw();
}

/* アクティブなペインを設定 */
void  CEditWnd::SetActivePane( int nIndex )
{
	assert_warning( nIndex < GetAllViewCount() );
	DEBUG_TRACE( L"CEditWnd::SetActivePane %d\n", nIndex );

	/* アクティブなビューを切り替える */
	int nOldIndex = m_nActivePaneIndex;
	m_nActivePaneIndex = nIndex;
	m_pcEditView = m_pcEditViewArr[m_nActivePaneIndex].get();

	// フォーカスを移動する	// 2007.10.16 ryoji
	GetView(nOldIndex).GetCaret().m_cUnderLine.CaretUnderLineOFF( true );	//	2002/05/11 YAZAKI
	if( ::GetActiveWindow() == GetHwnd()
		&& ::GetFocus() != GetActiveView().GetHwnd() )
	{
		// ::SetFocus()でフォーカスを切り替える
		::SetFocus( GetActiveView().GetHwnd() );
	}else{
		// 2010.04.08 ryoji
		// 起動と同時にエディットボックスにフォーカスのあるダイアログを表示すると当該エディットボックスに
		// キャレットが表示されない問題(*1)を修正するのため、内部的な切り替えをするのはアクティブペインが
		// 切り替わるときだけにした。← CEditView::OnKillFocus()は自スレッドのキャレットを破棄するので
		// (*1) -GREPDLGオプションによるGREPダイアログ表示や開ファイル後自動実行マクロでのInputBox表示
		if( m_nActivePaneIndex != nOldIndex ){
			// アクティブでないときに::SetFocus()するとアクティブになってしまう
			// （不可視なら可視になる）ので内部的に切り替えるだけにする
			GetView(nOldIndex).OnKillFocus();
			GetActiveView().OnSetFocus();
		}
	}

	GetActiveView().RedrawAll();	/* フォーカス移動時の再描画 */

	m_cSplitterWnd.SetActivePane( nIndex );
	RefreshCurlSyntaxErrorList();
	m_cToolbar.UpdateToolbar();

	if( nullptr != m_cDlgFind.GetHwnd() ){		/* 「検索」ダイアログ */
		/* モードレス時：検索対象となるビューの変更 */
		m_cDlgFind.ChangeView( (LPARAM)&GetActiveView() );
	}
	if( nullptr != m_cDlgReplace.GetHwnd() ){	/* 「置換」ダイアログ */
		/* モードレス時：検索対象となるビューの変更 */
		m_cDlgReplace.ChangeView( (LPARAM)&GetActiveView() );
	}
	if( nullptr != m_cHokanMgr.GetHwnd() ){	/* 「入力補完」ダイアログ */
		/* モードレス時：検索対象となるビューの変更 */
		m_cHokanMgr.ChangeView( (LPARAM)&GetActiveView() );
	}
	if( nullptr != m_cDlgFuncList.GetHwnd() ){	/* 「アウトライン」ダイアログ */ // 20060201 aroka
		/* モードレス時：現在位置表示の対象となるビューの変更 */
		m_cDlgFuncList.ChangeView( (LPARAM)&GetActiveView() );
	}

	return;
}

/** すべてのペインの描画スイッチを設定する

	@param bDraw [in] 描画スイッチの設定値

	@date 2008.06.08 ryoji 新規作成
*/
bool CEditWnd::SetDrawSwitchOfAllViews( bool bDraw )
{
	bool bDrawSwitchOld = GetActiveView().GetDrawSwitch();

	for (int i = 0; i < GetAllViewCount(); ++i) {
		GetView(i).SetDrawSwitch( bDraw );
	}
	m_cMiniMapView.SetDrawSwitch( bDraw );
	return bDrawSwitchOld;
}

/** すべてのペインをRedrawする

	スクロールバーの状態更新はパラメータでフラグ制御 or 別関数にしたほうがいい？
	@date 2007.07.22 ryoji スクロールバーの状態更新を追加

	@param pcViewExclude [in] Redrawから除外するビュー
	@date 2008.06.08 ryoji pcViewExclude パラメータ追加
*/
void CEditWnd::RedrawAllViews( CEditView* pcViewExclude )
{
	for (int i = 0; i < GetAllViewCount(); ++i) {
		const auto pcView = &GetView(i);
		if( pcView == pcViewExclude )
			continue;
		if( i == m_nActivePaneIndex ){
			pcView->RedrawAll();
		}else{
			pcView->Redraw();
			pcView->AdjustScrollBars();
		}
	}
	m_cMiniMapView.Redraw();
	m_cMiniMapView.AdjustScrollBars();
}

void CEditWnd::Views_DisableSelectArea([[maybe_unused]] bool bRedraw)
{
	for( int i = 0; i < GetAllViewCount(); ++i ){
		if( GetView(i).GetSelectionInfo().IsTextSelected() ){	/* テキストが選択されているか */
			/* 現在の選択範囲を非選択状態に戻す */
			GetView(i).GetSelectionInfo().DisableSelectArea( true );
		}
	}
}

/* すべてのペインで、行番号表示に必要な幅を再設定する（必要なら再描画する） */
BOOL CEditWnd::DetectWidthOfLineNumberAreaAllPane( bool bRedraw )
{
	if( 1 == GetAllViewCount() ){
		return GetActiveView().GetTextArea().DetectWidthOfLineNumberArea( bRedraw );
	}
	// 以下2,4分割限定

	if ( GetActiveView().GetTextArea().DetectWidthOfLineNumberArea( bRedraw ) ){
		/* ActivePaneで計算したら、再設定・再描画が必要と判明した */
		if ( m_cSplitterWnd.GetAllSplitCols() == 2 ){
			GetView(m_nActivePaneIndex^1).GetTextArea().DetectWidthOfLineNumberArea( bRedraw );
		}
		else {
			//	表示されていないので再描画しない
			GetView(m_nActivePaneIndex^1).GetTextArea().DetectWidthOfLineNumberArea( false );
		}
		if ( m_cSplitterWnd.GetAllSplitRows() == 2 ){
			GetView(m_nActivePaneIndex^2).GetTextArea().DetectWidthOfLineNumberArea( bRedraw );
			if ( m_cSplitterWnd.GetAllSplitCols() == 2 ){
				GetView((m_nActivePaneIndex^1)^2).GetTextArea().DetectWidthOfLineNumberArea( bRedraw );
			}
		}
		else {
			GetView(m_nActivePaneIndex^2).GetTextArea().DetectWidthOfLineNumberArea( false );
			GetView((m_nActivePaneIndex^1)^2).GetTextArea().DetectWidthOfLineNumberArea( false );
		}
		return TRUE;
	}
	return FALSE;
}

/** 右端で折り返す
	@param nViewColNum	[in] 右端で折り返すペインの番号
	@retval 折り返しを変更したかどうか
	@date 2008.06.08 ryoji 新規作成
*/
BOOL CEditWnd::WrapWindowWidth( int nPane )
{
	// 右端で折り返す
	CKetaXInt nWidth = GetView(nPane).ViewColNumToWrapColNum( GetView(nPane).GetTextArea().m_nViewColNum );
	if( GetDocument()->m_cLayoutMgr.GetMaxLineKetas() != nWidth ){
		ChangeLayoutParam( false, GetDocument()->m_cLayoutMgr.GetTabSpaceKetas(), GetDocument()->m_cLayoutMgr.m_tsvInfo.m_nTsvMode, nWidth );
		ClearViewCaretPosInfo();
		return TRUE;
	}
	return FALSE;
}

/** 折り返し方法関連の更新
	@retval 画面更新したかどうか
	@date 2008.06.10 ryoji 新規作成
*/
BOOL CEditWnd::UpdateTextWrap( void )
{
	// この関数はコマンド実行ごとに処理の最終段階で利用する
	// （アンドゥ登録＆全ビュー更新のタイミング）
	if( GetDocument()->m_nTextWrapMethodCur == WRAP_WINDOW_WIDTH ){
		BOOL bWrap = WrapWindowWidth( 0 );	// 右端で折り返す
		if( bWrap ){
			// WrapWindowWidth() で追加した更新リージョンで画面更新する
			for( int i = 0; i < GetAllViewCount(); i++ ){
				::UpdateWindow( GetView(i).GetHwnd() );
			}
			if( m_cMiniMapView.GetHwnd() ){
				::UpdateWindow( m_cMiniMapView.GetHwnd() );
			}
		}
		return bWrap;	// 画面更新＝折り返し変更
	}
	return FALSE;	// 画面更新しなかった
}

/*!	レイアウトパラメータの変更

	具体的にはタブ幅と折り返し位置を変更する．
	現在のドキュメントのレイアウトのみを変更し，共通設定は変更しない．

	@date 2005.08.14 genta 新規作成
	@date 2008.06.18 ryoji レイアウト変更途中はカーソル移動の画面スクロールを見せない（画面のちらつき抑止）
*/
void CEditWnd::ChangeLayoutParam( bool bShowProgress, CKetaXInt nTabSize, int nTsvMode, CKetaXInt nMaxLineKetas )
{
	HWND		hwndProgress = nullptr;
	if( bShowProgress && nullptr != this ){ // TODO: Remove "this" check
		hwndProgress = m_cStatusBar.GetProgressHwnd();
		//	Status Barが表示されていないときはm_hwndProgressBar == NULL
	}

	if( hwndProgress ){
		::ShowWindow( hwndProgress, SW_SHOW );
	}

	//	座標の保存
	CLogicPointEx* posSave = SavePhysPosOfAllView();

	//	レイアウトの更新
	GetDocument()->m_cLayoutMgr.ChangeLayoutParam( nTabSize, nTsvMode, nMaxLineKetas );
	ClearViewCaretPosInfo();

	//	座標の復元
	//	レイアウト変更途中はカーソル移動の画面スクロールを見せない	// 2008.06.18 ryoji
	const bool bDrawSwitchOld = SetDrawSwitchOfAllViews( false );
	RestorePhysPosOfAllView( posSave );
	SetDrawSwitchOfAllViews( bDrawSwitchOld );

	for( int i = 0; i < GetAllViewCount(); i++ ){
		if( GetView(i).GetHwnd() ){
			InvalidateRect( GetView(i).GetHwnd(), nullptr, TRUE );
			GetView(i).AdjustScrollBars();	// 2008.06.18 ryoji
		}
	}
	if( m_cMiniMapView.GetHwnd() ){
		InvalidateRect( m_cMiniMapView.GetHwnd(), nullptr, TRUE );
		m_cMiniMapView.AdjustScrollBars();
	}
	GetActiveView().GetCaret().ShowCaretPosInfo();	// 2009.07.25 ryoji

	if( hwndProgress ){
		::ShowWindow( hwndProgress, SW_HIDE );
	}
}

/*!
	レイアウトの変更に先立って，全てのViewの座標を物理座標に変換して保存する．

	@return データを保存した配列へのポインタ

	@note 取得した値はレイアウト変更後にCEditWnd::RestorePhysPosOfAllViewへ渡す．
	渡し忘れるとメモリリークとなる．

	@date 2005.08.11 genta  新規作成
	@date 2007.09.06 kobake 戻り値をCLogicPoint*に変更
	@date 2011.12.28 CLogicPointをCLogicPointExに変更。改行より右側でも復帰できるように
*/
CLogicPointEx* CEditWnd::SavePhysPosOfAllView()
{
	const int NUM_OF_VIEW = GetAllViewCount();
	const int NUM_OF_POS = 6;

	CLogicPointEx* pptPosArray = new CLogicPointEx[NUM_OF_VIEW * NUM_OF_POS];

	for( int i = 0; i < NUM_OF_VIEW; ++i ){
		CLayoutPoint tmp = CLayoutPoint(CLayoutInt(0), GetView(i).m_pcTextArea->GetViewTopLine());
		if (const auto layoutLine = GetDocument()->m_cLayoutMgr.SearchLineByLayoutY(tmp.GetY2())) {
			CLogicInt nLineCenter = layoutLine->GetLogicOffset() + layoutLine->GetLengthWithoutEOL() / 2;
			pptPosArray[i * NUM_OF_POS + 0].x = nLineCenter;
			pptPosArray[i * NUM_OF_POS + 0].y = layoutLine->GetLogicLineNo();
		}else{
			pptPosArray[i * NUM_OF_POS + 0].x = CLogicInt(0);
			pptPosArray[i * NUM_OF_POS + 0].y = CLogicInt(0);
		}
		pptPosArray[i * NUM_OF_POS + 0].ext = CLayoutInt(0);
		if( GetView(i).GetSelectionInfo().m_sSelectBgn.GetFrom().y >= 0 ){
			GetDocument()->m_cLayoutMgr.LayoutToLogicEx(
				GetView(i).GetSelectionInfo().m_sSelectBgn.GetFrom(),
				&pptPosArray[i * NUM_OF_POS + 1]
			);
		}
		if( GetView(i).GetSelectionInfo().m_sSelectBgn.GetTo().y >= 0 ){
			GetDocument()->m_cLayoutMgr.LayoutToLogicEx(
				GetView(i).GetSelectionInfo().m_sSelectBgn.GetTo(),
				&pptPosArray[i * NUM_OF_POS + 2]
			);
		}
		if( GetView(i).GetSelectionInfo().m_sSelect.GetFrom().y >= 0 ){
			GetDocument()->m_cLayoutMgr.LayoutToLogicEx(
				GetView(i).GetSelectionInfo().m_sSelect.GetFrom(),
				&pptPosArray[i * NUM_OF_POS + 3]
			);
		}
		if( GetView(i).GetSelectionInfo().m_sSelect.GetTo().y >= 0 ){
			GetDocument()->m_cLayoutMgr.LayoutToLogicEx(
				GetView(i).GetSelectionInfo().m_sSelect.GetTo(),
				&pptPosArray[i * NUM_OF_POS + 4]
			);
		}
		GetDocument()->m_cLayoutMgr.LayoutToLogicEx(
			GetView(i).GetCaret().GetCaretLayoutPos(),
			&pptPosArray[i * NUM_OF_POS + 5]
		);
	}
	return pptPosArray;
}

/*!	座標の復元

	CEditWnd::SavePhysPosOfAllViewで保存したデータを元に座標値を再計算する．

	@date 2005.08.11 genta  新規作成
	@date 2007.09.06 kobake 引数をCLogicPoint*に変更
	@date 2011.12.28 CLogicPointをCLogicPointExに変更。改行より右側でも復帰できるように
*/
void CEditWnd::RestorePhysPosOfAllView( CLogicPointEx* pptPosArray )
{
	const int NUM_OF_VIEW = GetAllViewCount();
	const int NUM_OF_POS = 6;

	for( int i = 0; i < NUM_OF_VIEW; ++i ){
		CLayoutPoint tmp;
		GetDocument()->m_cLayoutMgr.LogicToLayoutEx(
			pptPosArray[i * NUM_OF_POS + 0],
			&tmp
		);
		GetView(i).m_pcTextArea->SetViewTopLine(tmp.GetY2());

		if( GetView(i).GetSelectionInfo().m_sSelectBgn.GetFrom().y >= 0 ){
			GetDocument()->m_cLayoutMgr.LogicToLayoutEx(
				pptPosArray[i * NUM_OF_POS + 1],
				GetView(i).GetSelectionInfo().m_sSelectBgn.GetFromPointer()
			);
		}
		if( GetView(i).GetSelectionInfo().m_sSelectBgn.GetTo().y >= 0 ){
			GetDocument()->m_cLayoutMgr.LogicToLayoutEx(
				pptPosArray[i * NUM_OF_POS + 2],
				GetView(i).GetSelectionInfo().m_sSelectBgn.GetToPointer()
			);
		}
		if( GetView(i).GetSelectionInfo().m_sSelect.GetFrom().y >= 0 ){
			GetDocument()->m_cLayoutMgr.LogicToLayoutEx(
				pptPosArray[i * NUM_OF_POS + 3],
				GetView(i).GetSelectionInfo().m_sSelect.GetFromPointer()
			);
		}
		if( GetView(i).GetSelectionInfo().m_sSelect.GetTo().y >= 0 ){
			GetDocument()->m_cLayoutMgr.LogicToLayoutEx(
				pptPosArray[i * NUM_OF_POS + 4],
				GetView(i).GetSelectionInfo().m_sSelect.GetToPointer()
			);
		}
		CLayoutPoint ptPosXY;
		GetDocument()->m_cLayoutMgr.LogicToLayoutEx(
			pptPosArray[i * NUM_OF_POS + 5],
			&ptPosXY
		);
		GetView(i).GetCaret().MoveCursor( ptPosXY, false ); // 2013.06.05 bScrollをtrue=>falase
		GetView(i).GetCaret().m_nCaretPosX_Prev = GetView(i).GetCaret().GetCaretLayoutPos().GetX2();

		CLayoutInt nLeft = CLayoutInt(0);
		if( GetView(i).GetTextArea().m_nViewColNum < GetView(i).GetRightEdgeForScrollBar() ){
			nLeft = GetView(i).GetRightEdgeForScrollBar() - GetView(i).GetTextArea().m_nViewColNum;
		}
		if( nLeft < GetView(i).GetTextArea().GetViewLeftCol() ){
			GetView(i).GetTextArea().SetViewLeftCol( nLeft );
		}

		GetView(i).GetCaret().ShowEditCaret();
	}
	GetActiveView().GetCaret().ShowCaretPosInfo();
	delete[] pptPosArray;
}

/*!
	@brief マウスの状態をクリアする（ホイールスクロール有無状態をクリア）

	@note ホイール操作によるページスクロール・横スクロール対応のために追加。
		  ページスクロール・横スクロールありフラグをOFFする。

	@date 2009.01.17 nasukoji	新規作成
*/
void CEditWnd::ClearMouseState( void )
{
	SetPageScrollByWheel( FALSE );		// ホイール操作によるページスクロール有無
	SetHScrollByWheel( FALSE );			// ホイール操作による横スクロール有無
}

/*! ウィンドウ毎にアクセラレータテーブルを作成する
	@date 2009.08.15 Hidetaka Sakai, nasukoji
	@date 2013.10.19 novice 共有メモリの代わりにWine実行判定処理を呼び出す

	@note Wineでは別プロセスで作成したアクセラレータテーブルを使用することができない。
	      IsWine()によりプロセス毎にアクセラレータテーブルが作成されるようになる
	      ため、ショートカットキーやカーソルキーが正常に処理されるようになる。
*/
void CEditWnd::CreateAccelTbl( void )
{
	m_hAccel = CKeyBind::CreateAccerelator(
		m_pShareData->m_Common.m_sKeyBind.m_nKeyNameArrNum,
		m_pShareData->m_Common.m_sKeyBind.m_pKeyNameArr
	);

	if( nullptr == m_hAccel ){
		ErrorMessage(
			nullptr,
			LS(STR_ERR_DLGEDITWND01)
		);
	}
}

/*! ウィンドウ毎に作成したアクセラレータテーブルを破棄する
	@datet 2009.08.15 Hidetaka Sakai, nasukoji
*/
void CEditWnd::DeleteAccelTbl( void )
{
	if( m_hAccel ){
		m_hAccel = nullptr;
	}
}

//プラグインコマンドをエディタに登録する
void CEditWnd::RegisterPluginCommand( int idCommand )
{
	CPlug* plug = CJackManager::getInstance()->GetCommandById( idCommand );
	RegisterPluginCommand( plug );
}

//プラグインコマンドをエディタに登録する（一括）
void CEditWnd::RegisterPluginCommand()
{
	const CPlug::Array& plugs = CJackManager::getInstance()->GetPlugs( PP_COMMAND );
	for( CPlug::ArrayIter it = plugs.begin(); it != plugs.end(); it++ ) {
		RegisterPluginCommand( *it );
	}
}

//プラグインコマンドをエディタに登録する
void CEditWnd::RegisterPluginCommand( CPlug* plug )
{
	int iBitmap = CMenuDrawer::TOOLBAR_ICON_PLUGCOMMAND_DEFAULT - 1;
	if( !plug->m_sIcon.empty() ){
		iBitmap = m_cMenuDrawer.m_pcIcons->Add( plug->m_cPlugin.GetFilePath( plug->m_sIcon ).c_str() );
	}

	m_cMenuDrawer.AddToolButton( iBitmap, plug->GetFunctionCode() );
}

const LOGFONT& CEditWnd::GetLogfont(bool bTempSetting)
{
	if( bTempSetting && GetDocument()->m_blfCurTemp ){
		return GetDocument()->m_lfCur;
	}
	if (const auto bUseTypeFont = GetDocument()->m_cDocType.GetDocumentAttribute().m_bUseTypeFont) {
		return GetDocument()->m_cDocType.GetDocumentAttribute().m_lf;
	}
	return m_pShareData->m_Common.m_sView.m_lf;
}

int CEditWnd::GetFontPointSize(bool bTempSetting)
{
	if( bTempSetting && GetDocument()->m_blfCurTemp ){
		return GetDocument()->m_nPointSizeCur;
	}
	if (const auto bUseTypeFont = GetDocument()->m_cDocType.GetDocumentAttribute().m_bUseTypeFont) {
		return GetDocument()->m_cDocType.GetDocumentAttribute().m_nPointSize;
	}
	return m_pShareData->m_Common.m_sView.m_nPointSize;
}
ECharWidthCacheMode CEditWnd::GetLogfontCacheMode()
{
	if( GetDocument()->m_blfCurTemp ){
		return CWM_CACHE_LOCAL;
	}
	if (const auto bUseTypeFont = GetDocument()->m_cDocType.GetDocumentAttribute().m_bUseTypeFont) {
		return CWM_CACHE_LOCAL;
	}
	return CWM_CACHE_SHARE;
}

/*!
	@brief 現在のズーム倍率を取得
	@return 1.0を等倍とするズーム倍率
*/
double CEditWnd::GetFontZoom()
{
	if( GetDocument()->m_blfCurTemp ){
		return GetDocument()->m_nCurrentZoom;
	}else{
		return 1.0;
	}
}

void CEditWnd::ClearViewCaretPosInfo()
{
	for( int v = 0; v < GetAllViewCount(); ++v ){
		GetView(v).GetCaret().ClearCaretPosInfoCache();
	}
}
