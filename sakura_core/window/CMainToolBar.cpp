/*! @file */
/*
	Copyright (C) 2007, kobake, ryoji
	Copyright (C) 2008, kobake
	Copyright (C) 2010, Uchi, Moca
	Copyright (C) 2012, aroka, Uchi
	Copyright (C) 2018-2022, Sakura Editor Organization

	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "window/CMainToolBar.h"
#include "window/CEditWnd.h"
#include "CEditApp.h"
#include "util/os.h"
#include "util/window.h"
#include "uiparts/CImageListMgr.h"
#include "apiwrap/CommonControl.h"
#include "apiwrap/StdControl.h"
#include "CSelectLang.h"
#include "apiwrap/DarkMode.h"
#include "view/CEditView.h"
#include "doc/CEditDoc.h"
#include "view/colors/CWordMarker.h"

namespace {
constexpr UINT WORDMARKER_BUTTON_BASE = 0x7D00;
constexpr UINT WORDMARKER_AUTO_BUTTON = WORDMARKER_BUTTON_BASE + 32;
constexpr UINT WORDMARKER_CLEAR_BUTTON = WORDMARKER_BUTTON_BASE + 33;
constexpr UINT WORDMARKER_OVERFLOW_BUTTON = WORDMARKER_BUTTON_BASE + 34;
constexpr UINT WORDMARKER_PREVIOUS_BUTTON = WORDMARKER_BUTTON_BASE + 35;
constexpr UINT WORDMARKER_NEXT_BUTTON = WORDMARKER_BUTTON_BASE + 36;
constexpr UINT WORDMARKER_MENU_REMOVE_BASE = WORDMARKER_BUTTON_BASE + 64;
constexpr UINT WORDMARKER_MENU_AUTO = WORDMARKER_MENU_REMOVE_BASE + 32;
constexpr UINT WORDMARKER_MENU_PREVIOUS = WORDMARKER_MENU_REMOVE_BASE + 33;
constexpr UINT WORDMARKER_MENU_NEXT = WORDMARKER_MENU_REMOVE_BASE + 34;
constexpr UINT WORDMARKER_MENU_CLEAR = WORDMARKER_MENU_REMOVE_BASE + 35;
constexpr UINT CURL_SYNTAX_BUTTON = 0x7CFF;
}

CMainToolBar::CMainToolBar(CEditWnd* pOwner)
: m_pOwner(pOwner)
{
}

void CMainToolBar::Create( CImageListMgr* pcIcons )
{
	m_pcIcons = pcIcons;
}

/*! 検索ボックスでの処理 */
void CMainToolBar::ProcSearchBox( MSG *msg )
{
	if( msg->message == WM_KEYDOWN /* && ::GetParent( msg->hwnd ) == m_hwndSearchBox */ )
	{
		if( msg->wParam == VK_RETURN )  //リターンキー
		{
			//検索キーワードを取得
			std::wstring strText;
			if( 0 < GetSearchKey(strText) )	//キー文字列がある
			{
				if( strText.size() < _MAX_PATH ){
					//検索キーを登録
					CSearchKeywordManager().AddToSearchKeyArr( strText.c_str() );
				}
				m_pOwner->GetActiveView().m_strCurSearchKey = std::move(strText);
				m_pOwner->GetActiveView().m_bCurSearchUpdate = true;
				m_pOwner->GetActiveView().ChangeCurRegexp();

				//検索ボックスを更新	// 2010/6/6 Uchi
				AcceptSharedSearchKey();

				//::SetFocus( m_hWnd );	//先にフォーカスを移動しておかないとキャレットが消える
				m_pOwner->GetActiveView().SetFocus();

				// 検索開始時のカーソル位置登録条件を変更 02/07/28 ai start
				m_pOwner->GetActiveView().m_ptSrchStartPos_PHY = m_pOwner->GetActiveView().GetCaret().GetCaretLogicPos();
				// 02/07/28 ai end

				//次を検索
				m_pOwner->OnCommand( (WORD)0 /*メニュー*/, (WORD)F_SEARCH_NEXT, nullptr );
			}
		}
		else if( msg->wParam == VK_TAB )	//タブキー
		{
			//フォーカスを移動
			//	2004.10.27 MIK IME表示位置のずれ修正
			::SetFocus( m_pOwner->GetHwnd()  );
		}
	}
}

/*! サブクラス化したツールバーのウィンドウプロシージャ
	@author ryoji
	@date 2006.09.06 ryoji
*/
LRESULT CALLBACK CMainToolBar::ToolBarWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData )
{
	auto* self = reinterpret_cast<CMainToolBar*>(dwRefData);
	switch( msg )
	{
	case WM_COMMAND:
		if (self && LOWORD(wParam) == CURL_SYNTAX_BUTTON && HIWORD(wParam) == BN_CLICKED) {
			if (!self->m_pOwner->IsCurlSyntaxDocument()) {
				self->m_pOwner->SendStatusMessage(L"Curl ソースでのみ構文チェックを実行できます。");
				return 0;
			}
			self->m_pOwner->SetCurlSyntaxRealtime(!curlsyntax::IsRealtimeEnabled());
			self->RefreshCurlSyntaxControl();
			return 0;
		}
		if (self && HIWORD(wParam) == BN_CLICKED &&
			(LOWORD(wParam) >= WORDMARKER_BUTTON_BASE && LOWORD(wParam) <= WORDMARKER_NEXT_BUTTON)) {
			self->HandleMarkerControl(LOWORD(wParam));
			return 0;
		}
		break;
	case WM_CTLCOLORBTN:
		if (self) {
			const HWND button = reinterpret_cast<HWND>(lParam);
			if (button == self->m_hwndCurlSyntaxButton && self->m_hCurlSyntaxButtonBrush) {
				::SetBkMode(reinterpret_cast<HDC>(wParam), OPAQUE);
				::SetBkColor(reinterpret_cast<HDC>(wParam), self->m_cCurlSyntaxButtonBack);
				::SetTextColor(reinterpret_cast<HDC>(wParam), self->m_cCurlSyntaxButtonText);
				return reinterpret_cast<LRESULT>(self->m_hCurlSyntaxButtonBrush);
			}
			for (size_t i = 0; i < self->m_hwndMarkerButtons.size(); ++i) {
				if (self->m_hwndMarkerButtons[i] == button && i < self->m_markerButtonBrushes.size()) {
					::SetBkMode(reinterpret_cast<HDC>(wParam), OPAQUE);
					::SetBkColor(reinterpret_cast<HDC>(wParam), self->m_markerButtonBackColors[i]);
					::SetTextColor(reinterpret_cast<HDC>(wParam), self->m_markerButtonTextColors[i]);
					return reinterpret_cast<LRESULT>(self->m_markerButtonBrushes[i]);
				}
			}
			if (self->m_hwndMarkerAutoButton == button && self->m_markerButtonBrushes.size() > self->m_hwndMarkerButtons.size()) {
				const size_t i = self->m_hwndMarkerButtons.size();
				::SetBkMode(reinterpret_cast<HDC>(wParam), OPAQUE);
				::SetBkColor(reinterpret_cast<HDC>(wParam), self->m_markerButtonBackColors[i]);
				::SetTextColor(reinterpret_cast<HDC>(wParam), self->m_markerButtonTextColors[i]);
				return reinterpret_cast<LRESULT>(self->m_markerButtonBrushes[i]);
			}
		}
		break;
	case WM_SIZE:
		break;
	// WinXP Visual Style のときにツールバー上でのマウス左右ボタン同時押しで無応答になる
	//（マウスをキャプチャしたまま放さない） 問題を回避するために右ボタンを無視する
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
		return 0L;				// 右ボタンの UP/DOWN は本来のウィンドウプロシージャに渡さない

	case WM_DESTROY:
		// サブクラス化解除
		::RemoveWindowSubclass(hWnd, &ToolBarWndProc, uIdSubclass);
		return 0L;
	default:
		break;
	}
	const LRESULT result = ::DefSubclassProc(hWnd, msg, wParam, lParam);
	// Rebar updates its child-band geometry in the default procedure. Layout our
	// overlay controls only after that geometry has settled, or they can all be
	// hidden using the previous (often zero-width) client rectangle.
	if (self && (msg == WM_SIZE || msg == WM_WINDOWPOSCHANGED || msg == WM_SHOWWINDOW)) {
		self->LayoutMarkerControls();
	}
	return result;
}

LRESULT CALLBACK CMainToolBar::MarkerButtonWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR refData)
{
	auto* self = reinterpret_cast<CMainToolBar*>(refData);
	if (msg == WM_CONTEXTMENU && self && subclassId >= WORDMARKER_BUTTON_BASE && subclassId < WORDMARKER_AUTO_BUTTON) {
		auto& markers = CWordMarker::Instance();
		const size_t markerIndex = subclassId - WORDMARKER_BUTTON_BASE;
		if (markerIndex < markers.Entries().size()) {
			const auto& colors = self->m_pOwner->GetActiveView().GetDocument()->m_cDocType.GetDocumentAttribute().m_ColorInfoArr;
			std::vector<unsigned> palette;
			for (int index = COLORIDX_SEARCH + 1; index <= COLORIDX_SEARCHTAIL; ++index) {
				const auto& attr = colors[index].m_sColorAttr;
				if (attr.m_cTEXT != attr.m_cBACK) palette.push_back(static_cast<unsigned>(index - COLORIDX_SEARCH));
			}
			if (!palette.empty()) {
				const unsigned current = markers.Entries()[markerIndex].color;
				const auto found = std::find(palette.begin(), palette.end(), current);
				const size_t next = found == palette.end() ? 0 : (static_cast<size_t>(found - palette.begin()) + 1) % palette.size();
				markers.SetColor(markerIndex, palette[next]);
				self->m_pOwner->GetActiveView().Redraw();
				self->RefreshMarkerControls();
			}
		}
		return 0;
	}
	if (msg == WM_NCDESTROY) ::RemoveWindowSubclass(hWnd, &MarkerButtonWndProc, subclassId);
	return ::DefSubclassProc(hWnd, msg, wParam, lParam);
}

/* ツールバー作成
	@date @@@ 2002.01.03 YAZAKI m_tbMyButtonなどをCShareDataからCMenuDrawerへ移動したことによる修正。
	@date 2005.08.29 aroka ツールバーの折り返し
	@date 2006.06.17 ryoji ビジュアルスタイルが有効の場合はツールバーを Rebar に入れてサイズ変更時のちらつきを無くす
*/
void CMainToolBar::CreateToolBar( void )
{
	if( m_hwndToolBar )return;

	REBARINFO		rbi;
	REBARBANDINFO	rbBand;
	int				nFlag;
	TBBUTTON		tbb;
	int				i;
	int				nIdx;
	LONG_PTR		lToolType;
	nFlag = 0;

	// 2006.06.17 ryoji
	// Rebar ウィンドウの作成
	if( IsVisualStyle() ){	// ビジュアルスタイル有効
		m_hwndReBar = ::CreateWindowEx(
			WS_EX_TOOLWINDOW,
			REBARCLASSNAME, //レバーコントロール
			nullptr,
			WS_CHILD/* | WS_VISIBLE*/ | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |	// 2007.03.08 ryoji WS_VISIBLE 除去
			RBS_BANDBORDERS | CCS_NODIVIDER,
			0, 0, 0, 0,
			m_pOwner->GetHwnd(),
			nullptr,
			CEditApp::getInstance()->GetAppInstance(),
			nullptr
		);
		if (IsDarkModeActive()) {
			SetWindowTheme(m_hwndReBar, L"", L"");
			SendMessage(m_hwndReBar, RB_SETBKCOLOR, 0, DarkMode::getBackgroundColor());
		}

		if( nullptr == m_hwndReBar ){
			TopWarningMessage( m_pOwner->GetHwnd(), LS(STR_ERR_DLGEDITWND04) );
			return;
		}
		::SetWindowSubclass(m_hwndReBar, &ToolBarWndProc, 1, reinterpret_cast<DWORD_PTR>(this));

		if( GetDllShareData().m_Common.m_sToolBar.m_bToolBarIsFlat ){	/* フラットツールバーにする／しない */
			PreventVisualStyle( m_hwndReBar );	// ビジュアルスタイル非適用のフラットな Rebar にする
		}

		::ZeroMemory(&rbi, sizeof(rbi));
		rbi.cbSize = sizeof(rbi);
		ApiWrap::Rebar_SetbarInfo(m_hwndReBar, &rbi);

		nFlag = CCS_NORESIZE | CCS_NODIVIDER | CCS_NOPARENTALIGN | TBSTYLE_FLAT;	// ツールバーへの追加スタイル
	}

	/* ツールバーウィンドウの作成 */
	m_hwndToolBar = ::CreateWindowEx(
		0,
		TOOLBARCLASSNAME,
		nullptr,
		WS_CHILD/* | WS_VISIBLE*/ | WS_CLIPCHILDREN | /*WS_BORDER | */	// 2006.06.17 ryoji WS_CLIPCHILDREN 追加	// 2007.03.08 ryoji WS_VISIBLE 除去
/*		WS_EX_WINDOWEDGE| */
		TBSTYLE_TOOLTIPS |
//		TBSTYLE_WRAPABLE |
//		TBSTYLE_ALTDRAG |
//		CCS_ADJUSTABLE |
		nFlag,
		0, 0,
		0, 0,
		m_pOwner->GetHwnd(),
		(HMENU)ID_TOOLBAR,
		CEditApp::getInstance()->GetAppInstance(),
		nullptr
	);
	if( nullptr == m_hwndToolBar ){
		if( GetDllShareData().m_Common.m_sToolBar.m_bToolBarIsFlat ){	/* フラットツールバーにする／しない */
			GetDllShareData().m_Common.m_sToolBar.m_bToolBarIsFlat = FALSE;
		}
		TopWarningMessage( m_pOwner->GetHwnd(), LS(STR_ERR_DLGEDITWND05) );
		DestroyToolBar();	// 2006.06.17 ryoji
	}
	else{
		// 2006.09.06 ryoji ツールバーをサブクラス化する
		::SetWindowSubclass(m_hwndToolBar, &ToolBarWndProc, 0, reinterpret_cast<DWORD_PTR>(this));

		// pixel数をベタ書きするとHighDPI環境でずれるのでシステム値を取得して使う
		const int cxBorder = DpiScaleX( 1 );
		const int cyBorder = DpiScaleY( 1 );
		const int cxEdge = DpiScaleX( 1 );
		const int cyEdge = DpiScaleY( 1 );
		const int cxSmIcon = DpiScaleX( 16 );
		const int cySmIcon = DpiScaleY( 16 );
		const int cxToolButton = cxBorder + cxEdge + cxSmIcon + cxEdge + cxBorder;	//22
		const int cyToolButton = cyBorder + cyEdge + cySmIcon + cyEdge + cyBorder;	//22
		ApiWrap::Toolbar_SetButtonSize( m_hwndToolBar, cxToolButton, cyToolButton );	// 2009.10.01 ryoji 高DPI対応スケーリング
		ApiWrap::Toolbar_ButtonStructSize( m_hwndToolBar, sizeof(TBBUTTON) );
		/* ツールバーにボタンを追加 */
		int count = 0;	//@@@ 2002.06.15 MIK
		int nToolBarButtonNum = 0;// 2005/8/29 aroka
		//	From Here 2005.08.29 aroka
		// はじめにツールバー構造体の配列を作っておく
		const auto nButtons = GetDllShareData().m_Common.m_sToolBar.m_nToolBarButtonNum;

		TBBUTTON* pTbbArr = new TBBUTTON[nButtons];
		for (i = 0; i < nButtons; ++i) {
			nIdx = GetDllShareData().m_Common.m_sToolBar.m_nToolBarButtonIdxArr[i];
			pTbbArr[nToolBarButtonNum] = m_pOwner->GetMenuDrawer().getButton(nIdx);
			// セパレータが続くときはひとつにまとめる
			// 折り返しボタンもTBSTYLE_SEP属性を持っているので
			// 折り返しの前のセパレータは全て削除される．
			if( (pTbbArr[nToolBarButtonNum].fsStyle & TBSTYLE_SEP) && (nToolBarButtonNum!=0)){
				if( (pTbbArr[nToolBarButtonNum-1].fsStyle & TBSTYLE_SEP) ){
					pTbbArr[nToolBarButtonNum-1] = pTbbArr[nToolBarButtonNum];
					nToolBarButtonNum--;
				}
			}
			// 仮想折返しボタンがきたら直前のボタンに折返し属性を付ける
			if( pTbbArr[nToolBarButtonNum].fsState & TBSTATE_WRAP ){
				if( nToolBarButtonNum!=0 ){
					pTbbArr[nToolBarButtonNum-1].fsState |= TBSTATE_WRAP;
				}
				continue;
			}
			nToolBarButtonNum++;
		}
		//	To Here 2005.08.29 aroka

		BITMAPINFO bminfo = {};
		BITMAPINFOHEADER& bmih = bminfo.bmiHeader;
		bmih.biSize = sizeof(BITMAPINFOHEADER);
		bmih.biWidth = (LONG)cxSmIcon;
		bmih.biHeight = -(LONG)cySmIcon;
		bmih.biPlanes = 1;
		bmih.biBitCount = 32;
		bmih.biCompression = BI_RGB;
		HDC hdc = CreateCompatibleDC(nullptr);
		m_hImageList = ImageList_Create(cxSmIcon, cySmIcon, ILC_COLOR32, nButtons, 0);
		m_hDisabledImageList = ImageList_Create(cxSmIcon, cySmIcon, ILC_COLOR32, nButtons, 0);
		for (int j = 0; j < nToolBarButtonNum; ++j) {
			TBBUTTON& tbbItem = pTbbArr[j];
			if (tbbItem.fsStyle == TBSTYLE_BUTTON || tbbItem.fsStyle == TBSTYLE_DROPDOWN) {
				uint32_t* pBits;
				HBITMAP hBMP;
				{
					hBMP = ::CreateDIBSection(hdc, &bminfo, DIB_RGB_COLORS, (void**)&pBits, nullptr, 0);
					m_pcIcons->DrawToolIcon(pBits, tbbItem.iBitmap, true, cxSmIcon, cySmIcon);
					ImageList_Add(m_hImageList, hBMP, nullptr);
					::DeleteObject(hBMP);
				}
				{
					hBMP = ::CreateDIBSection(hdc, &bminfo, DIB_RGB_COLORS, (void**)&pBits, nullptr, 0);
					m_pcIcons->DrawToolIcon(pBits, tbbItem.iBitmap, false, cxSmIcon, cySmIcon);
					auto ret = ImageList_Add(m_hDisabledImageList, hBMP, nullptr);
					tbbItem.iBitmap = ret;
					::DeleteObject(hBMP);
				}
			}
		}
		::DeleteDC(hdc);

		ApiWrap::Toolbar_SetImageList(m_hwndToolBar, 0, m_hImageList);
		ApiWrap::Toolbar_SetDisabledImageList(m_hwndToolBar, 0, m_hDisabledImageList);

		for( i = 0; i < nToolBarButtonNum; ++i ){
			tbb = pTbbArr[i];

			//@@@ 2002.06.15 MIK start
			switch( tbb.fsStyle )
			{
			case TBSTYLE_DROPDOWN:	//ドロップダウン
				//拡張スタイルに設定
				ApiWrap::Toolbar_SetExtendedStyle( m_hwndToolBar, TBSTYLE_EX_DRAWDDARROWS );
				ApiWrap::Toolbar_AddButtons( m_hwndToolBar, 1, &tbb );
				count++;
				break;

			case TBSTYLE_COMBOBOX:	//コンボボックス
				{
					RECT			rc;
					TBBUTTONINFO	tbi;
					TBBUTTON		my_tbb;
					LOGFONT		lf;

					switch( tbb.idCommand )
					{
					case F_SEARCH_BOX:
						if( m_hwndSearchBox )
						{
							break;
						}
						
						//セパレータ作る
						memset_raw( &my_tbb, 0, sizeof(my_tbb) );
						my_tbb.fsStyle   = TBSTYLE_BUTTON;  //ボタンにしないと描画が乱れる 2005/8/29 aroka
						my_tbb.idCommand = tbb.idCommand;	//同じIDにしておく
						if( tbb.fsState & TBSTATE_WRAP ){   //折り返し 2005/8/29 aroka
							my_tbb.fsState |=  TBSTATE_WRAP;
						}
						ApiWrap::Toolbar_AddButtons( m_hwndToolBar, 1, &my_tbb );
						count++;

						//サイズを設定する
						tbi.cbSize = sizeof(tbi);
						tbi.dwMask = TBIF_SIZE;
						tbi.cx     = (WORD)DpiScaleX(160);	//ボックスの幅	// 2009.10.01 ryoji 高DPI対応スケーリング
						ApiWrap::Toolbar_SetButtonInfo( m_hwndToolBar, tbb.idCommand, &tbi );

						//位置とサイズを取得する
						rc.right = rc.left = rc.top = rc.bottom = 0;
						ApiWrap::Toolbar_GetItemRect( m_hwndToolBar, count-1, &rc );
						// Social Distance
						rc.left += cxBorder;
						rc.right -= cxBorder;

						//コンボボックスを作る
						m_hwndSearchBox = CreateWindow( WC_COMBOBOX, L"Combo",
								WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN
								/*| CBS_SORT*/ | CBS_AUTOHSCROLL /*| CBS_DISABLENOSCROLL*/,
								rc.left, rc.top, rc.right - rc.left, (rc.bottom - rc.top) * 10,
								m_hwndToolBar, (HMENU)(INT_PTR)tbb.idCommand, CEditApp::getInstance()->GetAppInstance(), nullptr );
						if( m_hwndSearchBox )
						{
							m_pOwner->SetCurrentFocus(0);

							lf = m_pOwner->GetLogfont();
							//memset_raw( &lf, 0, sizeof(lf) );
							lf.lfHeight			= DpiPointsToPixels(-9); // Jan. 14, 2003 genta ダイアログにあわせてちょっと小さく	// 2009.10.01 ryoji 高DPI対応（ポイント数から算出）
							lf.lfWidth			= 0;
							lf.lfEscapement		= 0;
							lf.lfOrientation	= 0;
							lf.lfWeight			= FW_NORMAL;
							lf.lfItalic			= FALSE;
							lf.lfUnderline		= FALSE;
							lf.lfStrikeOut		= FALSE;
							//lf.lfCharSet		= GetDllShareData().m_Common.m_sView.m_lf.lfCharSet;
							lf.lfOutPrecision	= OUT_TT_ONLY_PRECIS;		// Raster Font を使わないように
							//lf.lfClipPrecision	= GetDllShareData().m_Common.m_sView.m_lf.lfClipPrecision;
							//lf.lfQuality		= GetDllShareData().m_Common.m_sView.m_lf.lfQuality;
							//lf.lfPitchAndFamily	= GetDllShareData().m_Common.m_sView.m_lf.lfPitchAndFamily;
							//wcsncpy( lf.lfFaceName, GetDllShareData().m_Common.m_sView.m_lf.lfFaceName, int(std::size(lf.lfFaceName)));	// 画面のフォントに設定	2012/11/27 Uchi
							m_hFontSearchBox = ::CreateFontIndirect( &lf );
							if( m_hFontSearchBox )
							{
								::SendMessage( m_hwndSearchBox, WM_SETFONT, (WPARAM)m_hFontSearchBox, MAKELONG (TRUE, 0) );
							}

							// //入力長制限
							// Combo_LimitText( m_hwndSearchBox, (WPARAM)_MAX_PATH - 1 );

							//検索ボックスを更新	// 関数化 2010/6/6 Uchi
							AcceptSharedSearchKey();

							CDialog::SetComboBoxDeleter(m_hwndSearchBox, &m_cRecentSearch);

							// コンボボックスの垂直位置を調整する
							CMyRect rcCombo;
							::GetWindowRect( m_hwndSearchBox, &rcCombo );
							::SetWindowPos( m_hwndSearchBox, nullptr,
								rc.left,	//作ったときと同じ値を指定
								rc.top + (rc.bottom - rc.top - rcCombo.Height()) / 2,
								0,			//rcCombo.Width()のまま変えない
								0,			//rcCombo.Height()のまま変えない
								SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING );
						}
						break;

					default:
						break;
					}
				}
				break;

			case TBSTYLE_BUTTON:	//ボタン
			case TBSTYLE_SEP:		//セパレータ
			default:
				ApiWrap::Toolbar_AddButtons( m_hwndToolBar, 1, &tbb );
				count++;
				break;
			}
			//@@@ 2002.06.15 MIK end
		}
		ApiWrap::Toolbar_SetBitmapSize(m_hwndToolBar, cxSmIcon, cySmIcon);
		if( GetDllShareData().m_Common.m_sToolBar.m_bToolBarIsFlat ){	/* フラットツールバーにする／しない */
			lToolType = ::GetWindowLongPtr(m_hwndToolBar, GWL_STYLE);
			lToolType |= (TBSTYLE_FLAT);
			::SetWindowLongPtr(m_hwndToolBar, GWL_STYLE, lToolType);
			::InvalidateRect(m_hwndToolBar, nullptr, TRUE);
		}
		delete []pTbbArr;// 2005/8/29 aroka
	}

	// 2006.06.17 ryoji
	// ツールバーを Rebar に入れる
	if( m_hwndReBar && m_hwndToolBar ){
		// ツールバーの高さを取得する
		DWORD dwBtnSize = ApiWrap::Toolbar_GetButtonSize( m_hwndToolBar );
		DWORD dwRows = ApiWrap::Toolbar_GetRows( m_hwndToolBar );

		// バンド情報を設定する
		// 以前のプラットフォームに _WIN32_WINNT >= 0x0600 で定義される構造体のフルサイズを渡すと失敗する	// 2007.12.21 ryoji
		rbBand.cbSize = CCSIZEOF_STRUCT( REBARBANDINFO, wID );
		rbBand.fMask  = RBBIM_STYLE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE;
		rbBand.fStyle = RBBS_CHILDEDGE;
		rbBand.hwndChild  = m_hwndToolBar;	// ツールバー
		rbBand.cxMinChild = 0;
		rbBand.cyMinChild = HIWORD(dwBtnSize) * dwRows;
		rbBand.cx         = 250;

		// バンドを追加する
		ApiWrap::Rebar_InsertBand( m_hwndReBar, -1, &rbBand );
		::ShowWindow( m_hwndToolBar, SW_SHOW );
		DarkMode::setDarkWndSafe(m_hwndReBar);
	}

	return;
}

void CMainToolBar::DestroyToolBar( void )
{
	if (::IsWindow(m_hwndCurlSyntaxButton)) ::DestroyWindow(m_hwndCurlSyntaxButton);
	m_hwndCurlSyntaxButton = nullptr;
	if (m_hCurlSyntaxButtonBrush) {
		::DeleteObject(m_hCurlSyntaxButtonBrush);
		m_hCurlSyntaxButtonBrush = nullptr;
	}
	for (HWND button : m_hwndMarkerButtons) if (::IsWindow(button)) ::DestroyWindow(button);
	if (::IsWindow(m_hwndMarkerAutoButton)) ::DestroyWindow(m_hwndMarkerAutoButton);
	if (::IsWindow(m_hwndMarkerPreviousButton)) ::DestroyWindow(m_hwndMarkerPreviousButton);
	if (::IsWindow(m_hwndMarkerNextButton)) ::DestroyWindow(m_hwndMarkerNextButton);
	if (::IsWindow(m_hwndMarkerOverflowButton)) ::DestroyWindow(m_hwndMarkerOverflowButton);
	if (::IsWindow(m_hwndMarkerClearButton)) ::DestroyWindow(m_hwndMarkerClearButton);
	for (HBRUSH brush : m_markerButtonBrushes) if (brush) ::DeleteObject(brush);
	m_markerButtonBrushes.clear();
	if( m_hwndToolBar )
	{
		if( m_hwndSearchBox )
		{
			if( m_hFontSearchBox )
			{
				::DeleteObject( m_hFontSearchBox );
				m_hFontSearchBox = nullptr;
			}

			::DestroyWindow( m_hwndSearchBox );
			m_hwndSearchBox = nullptr;

			m_pOwner->SetCurrentFocus(0);
		}

		::DestroyWindow( m_hwndToolBar );
		m_hwndToolBar = nullptr;

		//if( m_cTabWnd.m_pOwner->GetHwnd() ) ::UpdateWindow( m_cTabWnd.m_pOwner->GetHwnd() );
	}

	// 2006.06.17 ryoji Rebar を破棄する
	if( m_hwndReBar )
	{
		::DestroyWindow( m_hwndReBar );
		m_hwndReBar = nullptr;
	}
	m_hwndMarkerButtons.clear();
	m_hwndMarkerAutoButton = nullptr;
	m_hwndMarkerPreviousButton = nullptr;
	m_hwndMarkerNextButton = nullptr;
	m_hwndMarkerOverflowButton = nullptr;
	m_hwndMarkerClearButton = nullptr;
	m_markerButtonWords.clear();
	m_markerButtonBackColors.clear();
	m_markerButtonTextColors.clear();
	m_markerButtonEnabled.clear();
	m_markerAutoWord.clear();
	m_markerAutoButtonVisible = false;

	if (m_hImageList)
	{
		ImageList_Destroy(m_hImageList);
		m_hImageList = nullptr;
	}
	if (m_hDisabledImageList)
	{
		ImageList_Destroy(m_hDisabledImageList);
		m_hDisabledImageList = nullptr;
	}

	return;
}

//! メッセージ処理。なんか処理したなら true を返す。
bool CMainToolBar::EatMessage(MSG* msg)
{
	if( m_hwndSearchBox && ::IsDialogMessage( m_hwndSearchBox, msg ) ){	//検索コンボボックス
		ProcSearchBox( msg );
		return true;
	}
	return false;
}

/*! ツールバー更新用タイマーの処理
	@date 2002.01.03 YAZAKI m_tbMyButtonなどをCShareDataからCMenuDrawerへ移動したことによる修正。
	@date 2003.08.29 wmlhq, ryoji nTimerCountの導入
	@date 2006.01.28 aroka OnTimerから分離
	@date 2007.04.03 ryoji パラメータ無しにした
	@date 2008.10.05 nasukoji ツールバー更新部分を外に出した
	@date 2012.11.29 aroka OnTimerから分離したときのバグ修正
*/
void CMainToolBar::OnToolbarTimer( void )
{
	// 2012.11.29 aroka ここではカウントアップ不要
	//m_pOwner->IncrementTimerCount(10);
	UpdateToolbar();	// 2008.09.23 nasukoji	ツールバーの表示を更新する
}

/*!
	@brief ツールバーの表示を更新する
	
	@note 他から呼べるようにOnToolbarTimer()より切り出した
	
	@date 2008.10.05 nasukoji
*/
void CMainToolBar::UpdateToolbar( void )
{
	// 印刷プレビュー中なら、何もしない。
	if( m_pOwner->IsInPreviewMode() )return;
	RefreshMarkerControls();
	
	// ツールバーの状態更新
	if( m_hwndToolBar )
	{
		for( int i = 0; i < GetDllShareData().m_Common.m_sToolBar.m_nToolBarButtonNum; ++i )
		{
			TBBUTTON tbb = m_pOwner->GetMenuDrawer().getButton(
				GetDllShareData().m_Common.m_sToolBar.m_nToolBarButtonIdxArr[i]
			);
			int state = ApiWrap::Toolbar_GetState( m_hwndToolBar, tbb.idCommand );
			if( state != -1 )
			{
				WORD stateToSet = state & ~(TBSTATE_ENABLED | TBSTATE_CHECKED);
				// 機能が利用可能か調べる
				if( IsFuncEnable( m_pOwner->GetDocument(), &GetDllShareData(), (EFunctionCode)tbb.idCommand ) )
				{
					stateToSet |= TBSTATE_ENABLED;
				}
				// 機能がチェック状態か調べる
				if( IsFuncChecked( m_pOwner->GetDocument(), &GetDllShareData(), (EFunctionCode)tbb.idCommand ) )
				{
					stateToSet |= TBSTATE_CHECKED;
				}
				if( state != stateToSet )
				{
					ApiWrap::Toolbar_SetState( m_hwndToolBar, tbb.idCommand, stateToSet );
				}
			}
		}
	}
}

void CMainToolBar::RefreshMarkerControls()
{
	if (!m_hwndToolBar || !::IsWindow(m_hwndToolBar)) return;
	RefreshCurlSyntaxControl();
	const auto& markers = CWordMarker::Instance();
	const auto& entries = markers.Entries();
	bool wordsChanged = entries.size() != m_markerButtonWords.size();
	for (size_t i = 0; !wordsChanged && i < entries.size(); ++i) wordsChanged = entries[i].word != m_markerButtonWords[i];
	const auto* automatic = markers.AutomaticWord(m_pOwner->GetActiveView().GetDocument());
	const bool hasAutomatic = automatic && !automatic->empty();
	static const std::wstring emptyWord;
	const std::wstring& autoWord = hasAutomatic ? *automatic : emptyWord;
	const auto& colors = m_pOwner->GetActiveView().GetDocument()->m_cDocType.GetDocumentAttribute().m_ColorInfoArr;
	auto resolveColor = [&](unsigned preferred) {
		if (preferred > 0 && preferred <= static_cast<unsigned>(COLORIDX_SEARCHTAIL - COLORIDX_SEARCH)) {
			const auto& attr = colors[COLORIDX_SEARCH + preferred].m_sColorAttr;
			if (attr.m_cTEXT != attr.m_cBACK) return std::pair<COLORREF, COLORREF>{ attr.m_cBACK, attr.m_cTEXT };
		}
		for (int index = COLORIDX_SEARCH + 1; index <= COLORIDX_SEARCHTAIL; ++index) {
			const auto& attr = colors[index].m_sColorAttr;
			if (attr.m_cTEXT != attr.m_cBACK) return std::pair<COLORREF, COLORREF>{ attr.m_cBACK, attr.m_cTEXT };
		}
		const auto& attr = colors[COLORIDX_SELECT].m_sColorAttr;
		return std::pair<COLORREF, COLORREF>{ attr.m_cBACK, attr.m_cTEXT };
	};
	std::vector<COLORREF> backColors, textColors;
	std::vector<bool> markerEnabled;
	for (const auto& entry : markers.Entries()) {
		const auto color = entry.enabled ? resolveColor(entry.color) : std::pair<COLORREF, COLORREF>{ RGB(210, 210, 210), RGB(110, 110, 110) };
		backColors.push_back(color.first);
		textColors.push_back(color.second);
		markerEnabled.push_back(entry.enabled);
	}
	if (hasAutomatic) {
		const auto& attr = colors[COLORIDX_DIFF_DELETE].m_sColorAttr;
		const auto color = std::pair<COLORREF, COLORREF>{ attr.m_cBACK, attr.m_cTEXT };
		backColors.push_back(color.first);
		textColors.push_back(color.second);
	} else {
		backColors.push_back(RGB(225, 225, 225));
		textColors.push_back(RGB(110, 110, 110));
	}
	if (!wordsChanged && hasAutomatic == m_markerAutoButtonVisible && autoWord == m_markerAutoWord &&
		backColors == m_markerButtonBackColors && textColors == m_markerButtonTextColors && markerEnabled == m_markerButtonEnabled &&
		m_hwndMarkerClearButton && m_hwndMarkerPreviousButton && m_hwndMarkerNextButton) {
		LayoutMarkerControls();
		return;
	}
	for (HWND button : m_hwndMarkerButtons) if (::IsWindow(button)) ::DestroyWindow(button);
	if (::IsWindow(m_hwndMarkerAutoButton)) ::DestroyWindow(m_hwndMarkerAutoButton);
	if (::IsWindow(m_hwndMarkerPreviousButton)) ::DestroyWindow(m_hwndMarkerPreviousButton);
	if (::IsWindow(m_hwndMarkerNextButton)) ::DestroyWindow(m_hwndMarkerNextButton);
	if (::IsWindow(m_hwndMarkerOverflowButton)) ::DestroyWindow(m_hwndMarkerOverflowButton);
	if (::IsWindow(m_hwndMarkerClearButton)) ::DestroyWindow(m_hwndMarkerClearButton);
	m_hwndMarkerButtons.clear();
	m_hwndMarkerAutoButton = nullptr;
	m_hwndMarkerPreviousButton = nullptr;
	m_hwndMarkerNextButton = nullptr;
	m_hwndMarkerOverflowButton = nullptr;
	m_hwndMarkerClearButton = nullptr;
	for (HBRUSH brush : m_markerButtonBrushes) if (brush) ::DeleteObject(brush);
	m_markerButtonBrushes.clear();
	m_markerButtonBackColors = backColors;
	m_markerButtonTextColors = textColors;
	m_markerButtonEnabled = markerEnabled;
	m_markerButtonWords.clear();
	m_markerButtonWords.reserve(entries.size());
	for (const auto& entry : entries) m_markerButtonWords.push_back(entry.word);
	m_markerAutoWord = autoWord;
	m_markerAutoButtonVisible = hasAutomatic;
	auto createButton = [&](const std::wstring& label, UINT id) {
		HWND parent = m_hwndReBar ? m_hwndReBar : m_hwndToolBar;
		return ::CreateWindowExW(0, L"BUTTON", label.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			0, 0, 1, 1, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), CEditApp::getInstance()->GetAppInstance(), nullptr);
	};
	for (size_t i = 0; i < entries.size(); ++i) {
		std::wstring label = (entries[i].enabled ? L"● " : L"○ ") + entries[i].word;
		HWND button = createButton(label, WORDMARKER_BUTTON_BASE + static_cast<UINT>(i));
		::SetWindowSubclass(button, &MarkerButtonWndProc, WORDMARKER_BUTTON_BASE + static_cast<UINT>(i), reinterpret_cast<DWORD_PTR>(this));
		m_hwndMarkerButtons.push_back(button);
		::SetWindowTheme(button, L"", L"");
		m_markerButtonBrushes.push_back(::CreateSolidBrush(backColors[i]));
	}
	const std::wstring autoLabel = hasAutomatic ? L"自動: " + *automatic : L"自動: なし";
	m_hwndMarkerAutoButton = createButton(autoLabel, WORDMARKER_AUTO_BUTTON);
	::SetWindowTheme(m_hwndMarkerAutoButton, L"", L"");
	::EnableWindow(m_hwndMarkerAutoButton, hasAutomatic);
	m_markerButtonBrushes.push_back(::CreateSolidBrush(backColors.back()));
	m_hwndMarkerPreviousButton = createButton(L"◀ 前へ", WORDMARKER_PREVIOUS_BUTTON);
	m_hwndMarkerNextButton = createButton(L"次へ ▶", WORDMARKER_NEXT_BUTTON);
	::EnableWindow(m_hwndMarkerPreviousButton, hasAutomatic);
	::EnableWindow(m_hwndMarkerNextButton, hasAutomatic);
	m_hwndMarkerOverflowButton = createButton(L"Marker", WORDMARKER_OVERFLOW_BUTTON);
	m_hwndMarkerClearButton = createButton(L"全解除", WORDMARKER_CLEAR_BUTTON);
	LayoutMarkerControls();
}

void CMainToolBar::RefreshCurlSyntaxControl()
{
	if (!m_hwndToolBar || !::IsWindow(m_hwndToolBar)) return;
	HWND parent = m_hwndReBar ? m_hwndReBar : m_hwndToolBar;
	if (!::IsWindow(m_hwndCurlSyntaxButton)) {
		m_hwndCurlSyntaxButton = ::CreateWindowExW(0, L"BUTTON", L"構文 OFF",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 1, 1,
			parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CURL_SYNTAX_BUTTON)),
			CEditApp::getInstance()->GetAppInstance(), nullptr);
		if (m_hwndCurlSyntaxButton) ::SetWindowTheme(m_hwndCurlSyntaxButton, L"", L"");
	}
	if (!m_hwndCurlSyntaxButton) return;
	const bool supported = m_pOwner->IsCurlSyntaxDocument();
	const bool enabled = curlsyntax::IsRealtimeEnabled();
	if (!m_bCurlSyntaxControlInitialized || supported != m_bCurlSyntaxSupported || enabled != m_bCurlSyntaxEnabled) {
		m_bCurlSyntaxControlInitialized = true;
		m_bCurlSyntaxSupported = supported;
		m_bCurlSyntaxEnabled = enabled;
		::EnableWindow(m_hwndCurlSyntaxButton, TRUE);
		::SetWindowTextW(m_hwndCurlSyntaxButton, enabled ? L"✓ 構文チェック" : L"構文チェック");
		m_cCurlSyntaxButtonBack = enabled ? RGB(170, 230, 170) : RGB(225, 225, 225);
		m_cCurlSyntaxButtonText = RGB(32, 32, 32);
		if (m_hCurlSyntaxButtonBrush) ::DeleteObject(m_hCurlSyntaxButtonBrush);
		m_hCurlSyntaxButtonBrush = ::CreateSolidBrush(m_cCurlSyntaxButtonBack);
		::InvalidateRect(m_hwndCurlSyntaxButton, nullptr, TRUE);
	}
	LayoutMarkerControls();
}

void CMainToolBar::LayoutMarkerControls()
{
	HWND parent = m_hwndReBar ? m_hwndReBar : m_hwndToolBar;
	if (!m_hwndToolBar || !::IsWindow(m_hwndToolBar) || !parent || !::IsWindow(parent)) return;
	RECT client{};
	::GetClientRect(parent, &client);
	int leftLimit = 0;
	if (m_hwndReBar) {
		RECT toolbar{};
		::GetWindowRect(m_hwndToolBar, &toolbar);
		::MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&toolbar), 2);
		leftLimit = toolbar.right + DpiScaleX(4);
	} else {
		const int count = static_cast<int>(::SendMessage(m_hwndToolBar, TB_BUTTONCOUNT, 0, 0));
		if (count > 0) {
			RECT last{};
			if (::SendMessage(m_hwndToolBar, TB_GETITEMRECT, count - 1, reinterpret_cast<LPARAM>(&last))) leftLimit = last.right + DpiScaleX(4);
		}
	}
	const int height = std::max(DpiScaleY(22), client.bottom - client.top);
	const int syntaxWidth = DpiScaleX(118);
	const int syntaxX = leftLimit;
	::ShowWindow(m_hwndCurlSyntaxButton, SW_SHOW);
	::SetWindowPos(m_hwndCurlSyntaxButton, HWND_TOP, syntaxX, (client.bottom - height) / 2,
		syntaxWidth, height, SWP_NOACTIVATE);
	const int markerLeftLimit = syntaxX + syntaxWidth + DpiScaleX(4);
	int right = client.right - DpiScaleX(2);
	auto place = [&](HWND button, int width) {
		if (!button) return false;
		right -= width;
		const bool fits = right >= markerLeftLimit;
		::ShowWindow(button, fits ? SW_SHOW : SW_HIDE);
		if (fits) ::SetWindowPos(button, HWND_TOP, right, (client.bottom - height) / 2, width, height, SWP_NOACTIVATE);
		right -= DpiScaleX(3);
		return fits;
	};
	// Keep a Marker entry point and navigation controls at the right edge. Word
	// chips use any remaining space and are reachable from the Marker menu.
	place(m_hwndMarkerOverflowButton, DpiScaleX(72));
	place(m_hwndMarkerClearButton, DpiScaleX(58));
	place(m_hwndMarkerNextButton, DpiScaleX(52));
	place(m_hwndMarkerPreviousButton, DpiScaleX(52));
	const int autoLabelLength = std::max(0, ::GetWindowTextLengthW(m_hwndMarkerAutoButton));
	place(m_hwndMarkerAutoButton, std::min(DpiScaleX(200), DpiScaleX(22 + autoLabelLength * 8)));
	size_t hiddenCount = 0;
	for (auto it = m_hwndMarkerButtons.rbegin(); it != m_hwndMarkerButtons.rend(); ++it) {
		const int labelLength = std::max(0, ::GetWindowTextLengthW(*it));
		const int chipWidth = std::min(DpiScaleX(200), DpiScaleX(22 + labelLength * 8));
		if (!place(*it, chipWidth)) ++hiddenCount;
	}
	const std::wstring label = hiddenCount > 0
		? L"Marker +" + std::to_wstring(hiddenCount) : L"Marker";
	::SetWindowTextW(m_hwndMarkerOverflowButton, label.c_str());
}

void CMainToolBar::HandleMarkerControl(UINT controlId)
{
	auto& markers = CWordMarker::Instance();
	if (controlId == WORDMARKER_CLEAR_BUTTON) {
		markers.ClearFixed();
		markers.ClearAutomatic(m_pOwner->GetActiveView().GetDocument());
	} else if (controlId == WORDMARKER_AUTO_BUTTON) {
		markers.ClearAutomatic(m_pOwner->GetActiveView().GetDocument());
	} else if (controlId == WORDMARKER_PREVIOUS_BUTTON || controlId == WORDMARKER_NEXT_BUTTON) {
		m_pOwner->NavigateAutomaticMarker(controlId == WORDMARKER_NEXT_BUTTON);
	} else if (controlId == WORDMARKER_OVERFLOW_BUTTON) {
		HMENU menu = ::CreatePopupMenu();
		if (menu) {
			for (size_t i = 0; i < markers.Entries().size(); ++i) {
				const UINT flags = MF_STRING | (markers.Entries()[i].enabled ? MF_CHECKED : MF_UNCHECKED);
				::AppendMenuW(menu, flags, WORDMARKER_MENU_REMOVE_BASE + static_cast<UINT>(i), markers.Entries()[i].word.c_str());
			}
			if (!markers.Entries().empty()) ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
			::AppendMenuW(menu, MF_STRING | (m_markerAutoButtonVisible ? MF_CHECKED : MF_UNCHECKED), WORDMARKER_MENU_AUTO,
				m_markerAutoButtonVisible ? L"自動 Marker を解除" : L"自動 Marker はありません");
			::EnableMenuItem(menu, WORDMARKER_MENU_AUTO, MF_BYCOMMAND | (m_markerAutoButtonVisible ? MF_ENABLED : MF_GRAYED));
			::AppendMenuW(menu, MF_STRING, WORDMARKER_MENU_PREVIOUS, L"自動 Marker の前へ");
			::AppendMenuW(menu, MF_STRING, WORDMARKER_MENU_NEXT, L"自動 Marker の次へ");
			::EnableMenuItem(menu, WORDMARKER_MENU_PREVIOUS, MF_BYCOMMAND | (m_markerAutoButtonVisible ? MF_ENABLED : MF_GRAYED));
			::EnableMenuItem(menu, WORDMARKER_MENU_NEXT, MF_BYCOMMAND | (m_markerAutoButtonVisible ? MF_ENABLED : MF_GRAYED));
			::AppendMenuW(menu, MF_STRING, WORDMARKER_MENU_CLEAR, L"Marker をすべて解除");
			RECT rc{};
			::GetWindowRect(m_hwndMarkerOverflowButton, &rc);
			const UINT selected = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, rc.left, rc.bottom, 0, m_pOwner->GetHwnd(), nullptr);
			::DestroyMenu(menu);
			if (selected >= WORDMARKER_MENU_REMOVE_BASE && selected < WORDMARKER_MENU_REMOVE_BASE + 32) {
				const size_t index = selected - WORDMARKER_MENU_REMOVE_BASE;
				if (index < markers.Entries().size()) {
					markers.ToggleEnabled(index);
				}
			} else if (selected == WORDMARKER_MENU_AUTO) {
				markers.ClearAutomatic(m_pOwner->GetActiveView().GetDocument());
			} else if (selected == WORDMARKER_MENU_PREVIOUS || selected == WORDMARKER_MENU_NEXT) {
				m_pOwner->NavigateAutomaticMarker(selected == WORDMARKER_MENU_NEXT);
			} else if (selected == WORDMARKER_MENU_CLEAR) {
				markers.ClearFixed();
				markers.ClearAutomatic(m_pOwner->GetActiveView().GetDocument());
			}
		}
	} else if (controlId >= WORDMARKER_BUTTON_BASE && controlId < WORDMARKER_AUTO_BUTTON) {
		const size_t index = controlId - WORDMARKER_BUTTON_BASE;
		if (index < markers.Entries().size()) {
			markers.ToggleEnabled(index);
		}
	}
	m_pOwner->GetActiveView().Redraw();
	RefreshMarkerControls();
}

//検索ボックスを更新
void CMainToolBar::AcceptSharedSearchKey()
{
	if( m_hwndSearchBox )
	{
		int	i;
		// 2013.05.28 Combo_ResetContentだとちらつくのでDeleteStringでリストだけ削除
		while (ApiWrap::Combo_GetCount(m_hwndSearchBox) > 0) {
			ApiWrap::Combo_DeleteString(m_hwndSearchBox, 0);
		}
		int nSize = GetDllShareData().m_sSearchKeywords.m_aSearchKeys.size();
		for( i = 0; i < nSize; i++ )
		{
			ApiWrap::Combo_AddString( m_hwndSearchBox, GetDllShareData().m_sSearchKeywords.m_aSearchKeys[i] );
		}
		const wchar_t* pszText;
		if( (GetDllShareData().m_Common.m_sSearch.m_bInheritKeyOtherView
			&& m_pOwner->GetActiveView().m_nCurSearchKeySequence < GetDllShareData().m_Common.m_sSearch.m_nSearchKeySequence)
				|| 0 == m_pOwner->GetActiveView().m_strCurSearchKey.size() ){
			if( 0 < nSize ){
				pszText = GetDllShareData().m_sSearchKeywords.m_aSearchKeys[0];
			}else{
				pszText = L"";
			}
		}else{
			pszText = m_pOwner->GetActiveView().m_strCurSearchKey.c_str();
		}
		std::wstring strText;
		GetSearchKey(strText);
		if( 0 < nSize && 0 != strText.compare(pszText) ){
			::SetWindowText(m_hwndSearchBox, pszText);
		}
	}
}

size_t CMainToolBar::GetSearchKey(std::wstring& strText)
{
	if( m_hwndSearchBox ){
		ApiWrap::Wnd_GetText( m_hwndSearchBox, strText );
	}else{
		strText.clear();
	}
	return strText.length();
}

/*!
ツールバーの検索ボックスにフォーカスを移動する.
	@date 2006.06.04 yukihane 新規作成
*/
void CMainToolBar::SetFocusSearchBox( void ) const
{
	if( m_hwndSearchBox ){
		::SetFocus(m_hwndSearchBox);
	}
}
