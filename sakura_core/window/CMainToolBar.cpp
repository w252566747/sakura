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

#include <algorithm>

namespace {
constexpr UINT WORDMARKER_BUTTON_BASE = 0x7D00;
constexpr UINT WORDMARKER_AUTO_BUTTON = WORDMARKER_BUTTON_BASE + 32;
constexpr UINT WORDMARKER_CLEAR_BUTTON = WORDMARKER_BUTTON_BASE + 33;
constexpr UINT WORDMARKER_OVERFLOW_BUTTON = WORDMARKER_BUTTON_BASE + 34;
constexpr UINT WORDMARKER_PREVIOUS_BUTTON = WORDMARKER_BUTTON_BASE + 35;
constexpr UINT WORDMARKER_NEXT_BUTTON = WORDMARKER_BUTTON_BASE + 36;
constexpr UINT WORDMARKER_SPACER_BUTTON = WORDMARKER_BUTTON_BASE + 37;
constexpr UINT WORDMARKER_MENU_REMOVE_BASE = WORDMARKER_BUTTON_BASE + 64;
constexpr UINT WORDMARKER_MENU_AUTO = WORDMARKER_MENU_REMOVE_BASE + 32;
constexpr UINT WORDMARKER_MENU_PREVIOUS = WORDMARKER_MENU_REMOVE_BASE + 33;
constexpr UINT WORDMARKER_MENU_NEXT = WORDMARKER_MENU_REMOVE_BASE + 34;
constexpr UINT WORDMARKER_MENU_CLEAR = WORDMARKER_MENU_REMOVE_BASE + 35;
constexpr UINT CURL_SYNTAX_BUTTON = 0x7CFF;
constexpr int MAX_VISIBLE_MARKERS = 4;
constexpr UINT ENHANCEMENT_TOOLBAR_ID = 0x7CFE;

HBITMAP CreateNavigationArrowBitmap(bool left, int width, int height)
{
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	HDC screenDc = ::GetDC(nullptr);
	DWORD* pixels = nullptr;
	HBITMAP bitmap = ::CreateDIBSection(screenDc, &info, DIB_RGB_COLORS,
		reinterpret_cast<void**>(&pixels), nullptr, 0);
	if (screenDc) ::ReleaseDC(nullptr, screenDc);
	if (!bitmap || !pixels) return bitmap;
	const int x0 = std::max(1, width / 16);
	const int x1 = std::max(x0 + 1, width * 7 / 16);
	const int x2 = std::max(x1 + 1, width * 15 / 16);
	const int y0 = std::max(1, height / 8);
	const int y1 = std::max(y0 + 1, height * 5 / 16);
	const int y2 = std::max(y1 + 1, height * 11 / 16);
	const int y3 = std::min(height - 2, height * 15 / 16);
	POINT points[7] = { {x0, height / 2}, {x1, y0}, {x1, y1}, {x2, y1},
		{x2, y2}, {x1, y2}, {x1, y3} };
	if (!left) for (auto& point : points) point.x = width - 1 - point.x;
	auto inside = [&](int x, int y) {
		bool result = false;
		for (int i = 0, j = 6; i < 7; j = i++) {
			const POINT& a = points[i];
			const POINT& b = points[j];
			if (((a.y > y) != (b.y > y)) &&
				(x < (b.x - a.x) * (y - a.y) / static_cast<double>(b.y - a.y) + a.x)) result = !result;
		}
		return result;
	};
	const COLORREF fill = RGB(0, 120, 212);
	const COLORREF edge = RGB(0, 78, 145);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const bool body = inside(x, y);
			bool border = false;
			if (body) {
				for (int dy = -1; dy <= 1 && !border; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						if (!inside(x + dx, y + dy)) { border = true; break; }
					}
				}
			}
			const COLORREF color = border ? edge : fill;
			pixels[y * width + x] = body ? 0xff000000 | GetRValue(color) << 16 | GetGValue(color) << 8 | GetBValue(color) : 0;
		}
	}
	return bitmap;
}

HBITMAP CreateMarkerSwatchBitmap(COLORREF color, int width, int height)
{
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	HDC screenDc = ::GetDC(nullptr);
	DWORD* pixels = nullptr;
	HBITMAP bitmap = ::CreateDIBSection(screenDc, &info, DIB_RGB_COLORS,
		reinterpret_cast<void**>(&pixels), nullptr, 0);
	if (screenDc) ::ReleaseDC(nullptr, screenDc);
	if (!bitmap || !pixels) return bitmap;
	const DWORD edge = RGB(72, 72, 72);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const bool border = x == 1 || y == 1 || x == width - 2 || y == height - 2;
			const bool inside = x >= 2 && y >= 2 && x < width - 2 && y < height - 2;
			const COLORREF pixel = border ? edge : inside ? color : RGB(0, 0, 0);
			const DWORD alpha = border || inside ? 0xff000000 : 0;
			pixels[y * width + x] = alpha | GetRValue(pixel) << 16 | GetGValue(pixel) << 8 | GetBValue(pixel);
		}
	}
	return bitmap;
}
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
LRESULT CALLBACK CMainToolBar::ToolBarWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	auto* self = reinterpret_cast<CMainToolBar*>(dwRefData);
	switch (msg) {
	case WM_COMMAND:
		if (self && reinterpret_cast<HWND>(lParam) == self->m_hwndEnhancementToolBar && HIWORD(wParam) == 0) {
			const UINT command = LOWORD(wParam);
			if (command == CURL_SYNTAX_BUTTON) {
				if (!self->m_pOwner->IsCurlSyntaxDocument()) {
					self->m_pOwner->SendStatusMessage(L"Curl ソースでのみ構文チェックを実行できます。");
					return 0;
				}
				self->m_pOwner->SetCurlSyntaxRealtime(!curlsyntax::IsRealtimeEnabled());
				self->RefreshCurlSyntaxControl();
				return 0;
			}
			if ((command >= WORDMARKER_BUTTON_BASE && command <= WORDMARKER_NEXT_BUTTON) ||
				command == WORDMARKER_CLEAR_BUTTON || command == WORDMARKER_OVERFLOW_BUTTON || command == WORDMARKER_AUTO_BUTTON) {
				self->HandleMarkerControl(command);
				return 0;
			}
		}
		break;
	case WM_RBUTTONDOWN:
		if (self && hWnd == self->m_hwndEnhancementToolBar) {
			POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			const int index = static_cast<int>(::SendMessage(hWnd, TB_HITTEST, 0, reinterpret_cast<LPARAM>(&point)));
			TBBUTTON button{};
			if (index >= 0 && ::SendMessage(hWnd, TB_GETBUTTON, index, reinterpret_cast<LPARAM>(&button)) &&
				button.idCommand >= WORDMARKER_BUTTON_BASE && button.idCommand < WORDMARKER_AUTO_BUTTON) {
				self->HandleMarkerColorChange(button.idCommand);
			}
			return 0L;
		}
		if (!self || hWnd != self->m_pOwner->GetHwnd()) return 0L;
		break;
	case WM_RBUTTONUP:
		if (self && hWnd == self->m_hwndEnhancementToolBar) return 0L;
		if (!self || hWnd != self->m_pOwner->GetHwnd()) return 0L;
		break;
	case WM_SIZE:
		break;
	case WM_DESTROY:
		::RemoveWindowSubclass(hWnd, &ToolBarWndProc, uIdSubclass);
		if (self && hWnd == self->m_pOwner->GetHwnd()) break;
		return 0L;
	default:
		break;
	}
	const LRESULT result = ::DefSubclassProc(hWnd, msg, wParam, lParam);
	if (self && hWnd != self->m_hwndEnhancementToolBar &&
		(msg == WM_SIZE || msg == WM_WINDOWPOSCHANGED || msg == WM_SHOWWINDOW)) {
		self->LayoutMarkerControls();
	}
	return result;
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
	// The editor window owns the unused area to the right of the native toolbar.
	// The matching auxiliary toolbar occupies that area so Rebar clipping cannot hide it.
	::SetWindowSubclass(m_pOwner->GetHwnd(), &ToolBarWndProc, 2, reinterpret_cast<DWORD_PTR>(this));

	return;
}

void CMainToolBar::DestroyToolBar( void )
{
	if (m_pOwner && ::IsWindow(m_pOwner->GetHwnd())) {
		::RemoveWindowSubclass(m_pOwner->GetHwnd(), &ToolBarWndProc, 2);
	}
	if (::IsWindow(m_hwndEnhancementToolBar)) {
		::RemoveWindowSubclass(m_hwndEnhancementToolBar, &ToolBarWndProc, 3);
		::DestroyWindow(m_hwndEnhancementToolBar);
	}
	m_hwndEnhancementToolBar = nullptr;
	if (m_hEnhancementImageList) {
		::ImageList_Destroy(m_hEnhancementImageList);
		m_hEnhancementImageList = nullptr;
	}
	m_nEnhancementMarkerStart = -1;
	m_bCurlSyntaxControlInitialized = false;
	m_markerButtonWords.clear();
	m_markerButtonBackColors.clear();
	m_markerButtonEnabled.clear();
	m_markerAutoWord.clear();
	m_markerAutoButtonVisible = false;
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
	if (!::IsWindow(m_hwndEnhancementToolBar)) return;

	auto& markers = CWordMarker::Instance();
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
			if (attr.m_cTEXT != attr.m_cBACK) return attr.m_cBACK;
		}
		for (int index = COLORIDX_SEARCH + 1; index <= COLORIDX_SEARCHTAIL; ++index) {
			const auto& attr = colors[index].m_sColorAttr;
			if (attr.m_cTEXT != attr.m_cBACK) return attr.m_cBACK;
		}
		return colors[COLORIDX_SELECT].m_sColorAttr.m_cBACK;
	};
	std::vector<COLORREF> markerColors;
	std::vector<bool> markerEnabled;
	for (const auto& entry : entries) {
		// Disabled markers keep their chosen color, but show a neutral swatch while inactive.
		markerColors.push_back(entry.enabled ? resolveColor(entry.color) : RGB(210, 210, 210));
		markerEnabled.push_back(entry.enabled);
	}
	COLORREF autoColor = RGB(255, 233, 172);
	if (hasAutomatic) autoColor = colors[COLORIDX_DIFF_DELETE].m_sColorAttr.m_cBACK;
	const int currentButtonCount = static_cast<int>(::SendMessage(m_hwndEnhancementToolBar, TB_BUTTONCOUNT, 0, 0));
	const bool toolbarNeedsBuild = currentButtonCount <= m_nEnhancementMarkerStart;
	const bool layoutChanged = toolbarNeedsBuild || wordsChanged || hasAutomatic != m_markerAutoButtonVisible || autoWord != m_markerAutoWord;
	const bool colorsChanged = markerColors != m_markerButtonBackColors || autoColor != m_markerAutoColor;
	const bool enabledChanged = markerEnabled != m_markerButtonEnabled;
	::SendMessage(m_hwndEnhancementToolBar, TB_ENABLEBUTTON, WORDMARKER_PREVIOUS_BUTTON, hasAutomatic);
	::SendMessage(m_hwndEnhancementToolBar, TB_ENABLEBUTTON, WORDMARKER_NEXT_BUTTON, hasAutomatic);
	if (!layoutChanged) {
		if (colorsChanged && m_hEnhancementImageList) {
			const size_t visible = std::min(entries.size(), static_cast<size_t>(MAX_VISIBLE_MARKERS));
			const int iconWidth = DpiScaleX(16);
			const int iconHeight = DpiScaleY(16);
			HIMAGELIST replacement = ::ImageList_Create(iconWidth, iconHeight, ILC_COLOR32,
				std::max(2, static_cast<int>(visible + (hasAutomatic ? 1 : 0) + 2)), 1);
			auto appendArrow = [&](bool left) {
				HBITMAP bitmap = CreateNavigationArrowBitmap(left, iconWidth, iconHeight);
				if (!bitmap) return I_IMAGENONE;
				const int index = replacement ? ::ImageList_Add(replacement, bitmap, nullptr) : I_IMAGENONE;
				::DeleteObject(bitmap);
				return index < 0 ? I_IMAGENONE : index;
			};
			const int previousArrowIndex = appendArrow(true);
			const int nextArrowIndex = appendArrow(false);
			std::vector<int> imageIndices;
			auto appendSwatch = [&](COLORREF color) {
				HBITMAP bitmap = CreateMarkerSwatchBitmap(color, DpiScaleX(16), DpiScaleY(16));
				if (!bitmap) return I_IMAGENONE;
				const int index = replacement ? ::ImageList_Add(replacement, bitmap, nullptr) : I_IMAGENONE;
				::DeleteObject(bitmap);
				return index < 0 ? I_IMAGENONE : index;
			};
			for (size_t i = 0; i < visible; ++i) imageIndices.push_back(appendSwatch(markerColors[i]));
			const int autoImageIndex = hasAutomatic ? appendSwatch(autoColor) : I_IMAGENONE;
			const size_t expectedImages = 2 + visible + (hasAutomatic ? 1 : 0);
			if (replacement && previousArrowIndex == 0 && nextArrowIndex == 1 && imageIndices.size() == visible &&
				std::all_of(imageIndices.begin(), imageIndices.end(), [](int index) { return index != I_IMAGENONE; }) &&
				(!hasAutomatic || autoImageIndex != I_IMAGENONE) &&
				static_cast<size_t>(::ImageList_GetImageCount(replacement)) == expectedImages) {
				HIMAGELIST oldImageList = m_hEnhancementImageList;
				::SendMessage(m_hwndEnhancementToolBar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(replacement));
				m_hEnhancementImageList = replacement;
				TBBUTTONINFO previousInfo{};
				previousInfo.cbSize = sizeof(previousInfo);
				previousInfo.dwMask = TBIF_IMAGE;
				previousInfo.iImage = previousArrowIndex;
				::SendMessage(m_hwndEnhancementToolBar, TB_SETBUTTONINFO, WORDMARKER_PREVIOUS_BUTTON, reinterpret_cast<LPARAM>(&previousInfo));
				TBBUTTONINFO nextInfo{};
				nextInfo.cbSize = sizeof(nextInfo);
				nextInfo.dwMask = TBIF_IMAGE;
				nextInfo.iImage = nextArrowIndex;
				::SendMessage(m_hwndEnhancementToolBar, TB_SETBUTTONINFO, WORDMARKER_NEXT_BUTTON, reinterpret_cast<LPARAM>(&nextInfo));
				for (size_t i = 0; i < visible; ++i) {
					TBBUTTONINFO info{};
					info.cbSize = sizeof(info);
					info.dwMask = TBIF_IMAGE;
					info.iImage = imageIndices[i];
					::SendMessage(m_hwndEnhancementToolBar, TB_SETBUTTONINFO,
						WORDMARKER_BUTTON_BASE + static_cast<UINT>(i), reinterpret_cast<LPARAM>(&info));
				}
				if (hasAutomatic) {
					TBBUTTONINFO info{};
					info.cbSize = sizeof(info);
					info.dwMask = TBIF_IMAGE;
					info.iImage = autoImageIndex;
					::SendMessage(m_hwndEnhancementToolBar, TB_SETBUTTONINFO,
						WORDMARKER_AUTO_BUTTON, reinterpret_cast<LPARAM>(&info));
				}
				if (oldImageList) ::ImageList_Destroy(oldImageList);
				::InvalidateRect(m_hwndEnhancementToolBar, nullptr, TRUE);
			} else if (replacement) {
				::ImageList_Destroy(replacement);
			}
		}
		if (enabledChanged) {
			const size_t visible = std::min(entries.size(), static_cast<size_t>(MAX_VISIBLE_MARKERS));
			for (size_t i = 0; i < visible; ++i) {
				::SendMessage(m_hwndEnhancementToolBar, TB_CHECKBUTTON,
					WORDMARKER_BUTTON_BASE + static_cast<UINT>(i), MAKELONG(entries[i].enabled, 0));
			}
		}
		m_markerButtonBackColors = markerColors;
		m_markerButtonEnabled = markerEnabled;
		m_markerAutoColor = autoColor;
		m_markerAutoWord = autoWord;
		m_markerAutoButtonVisible = hasAutomatic;
		LayoutMarkerControls();
		return;
	}

	const int markerStart = m_nEnhancementMarkerStart;
	int buttonCount = static_cast<int>(::SendMessage(m_hwndEnhancementToolBar, TB_BUTTONCOUNT, 0, 0));
	while (buttonCount > markerStart) {
		::SendMessage(m_hwndEnhancementToolBar, TB_DELETEBUTTON, buttonCount - 1, 0);
		--buttonCount;
	}
	if (m_hEnhancementImageList) {
		::SendMessage(m_hwndEnhancementToolBar, TB_SETIMAGELIST, 0, 0);
		::ImageList_Destroy(m_hEnhancementImageList);
		m_hEnhancementImageList = nullptr;
	}
	const int iconWidth = DpiScaleX(16);
	const int iconHeight = DpiScaleY(16);
	const size_t visibleMarkerCount = std::min(entries.size(), static_cast<size_t>(MAX_VISIBLE_MARKERS));
	m_hEnhancementImageList = ::ImageList_Create(iconWidth, iconHeight, ILC_COLOR32,
		std::max(2, static_cast<int>(visibleMarkerCount + (hasAutomatic ? 1 : 0) + 2)), 1);
	auto appendArrow = [&](bool left) {
		HBITMAP bitmap = CreateNavigationArrowBitmap(left, iconWidth, iconHeight);
		if (!bitmap) return I_IMAGENONE;
		const int index = m_hEnhancementImageList ? ::ImageList_Add(m_hEnhancementImageList, bitmap, nullptr) : I_IMAGENONE;
		::DeleteObject(bitmap);
		return index < 0 ? I_IMAGENONE : index;
	};
	const int previousArrowIndex = appendArrow(true);
	const int nextArrowIndex = appendArrow(false);
	TBBUTTONINFO previousInfo{};
	previousInfo.cbSize = sizeof(previousInfo);
	previousInfo.dwMask = TBIF_IMAGE;
	previousInfo.iImage = previousArrowIndex;
	::SendMessage(m_hwndEnhancementToolBar, TB_SETBUTTONINFO, WORDMARKER_PREVIOUS_BUTTON, reinterpret_cast<LPARAM>(&previousInfo));
	TBBUTTONINFO nextInfo{};
	nextInfo.cbSize = sizeof(nextInfo);
	nextInfo.dwMask = TBIF_IMAGE;
	nextInfo.iImage = nextArrowIndex;
	::SendMessage(m_hwndEnhancementToolBar, TB_SETBUTTONINFO, WORDMARKER_NEXT_BUTTON, reinterpret_cast<LPARAM>(&nextInfo));
	std::vector<int> imageIndices;
	auto appendSwatch = [&](COLORREF color) {
		HBITMAP bitmap = CreateMarkerSwatchBitmap(color, iconWidth, iconHeight);
		if (!bitmap) return I_IMAGENONE;
		const int index = m_hEnhancementImageList ? ::ImageList_Add(m_hEnhancementImageList, bitmap, nullptr) : I_IMAGENONE;
		::DeleteObject(bitmap);
		return index < 0 ? I_IMAGENONE : index;
	};
	for (size_t i = 0; i < visibleMarkerCount; ++i) imageIndices.push_back(appendSwatch(markerColors[i]));
	const int autoImageIndex = hasAutomatic ? appendSwatch(autoColor) : I_IMAGENONE;
	::SendMessage(m_hwndEnhancementToolBar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(m_hEnhancementImageList));

	std::vector<std::wstring> labels;
	labels.reserve(visibleMarkerCount + (hasAutomatic ? 1 : 0) + 2);
	auto displayWord = [](const std::wstring& word) {
		return word.size() > 14 ? word.substr(0, 13) + L"…" : word;
	};
	for (size_t i = 0; i < visibleMarkerCount; ++i) labels.push_back(displayWord(entries[i].word));
	if (hasAutomatic) labels.push_back(L"自動: " + displayWord(autoWord));
	labels.push_back(L"全解除");
	const size_t hiddenCount = entries.size() - visibleMarkerCount;
	labels.push_back(hiddenCount ? L"Marker +" + std::to_wstring(hiddenCount) : L"Marker");

	std::wstring stringTable;
	for (const auto& label : labels) {
		stringTable.append(label);
		stringTable.push_back(L'\0');
	}
	stringTable.push_back(L'\0');
	const int firstString = static_cast<int>(::SendMessageW(m_hwndEnhancementToolBar,
		TB_ADDSTRINGW, 0, reinterpret_cast<LPARAM>(stringTable.c_str())));
	std::vector<TBBUTTON> buttons;
	buttons.reserve(labels.size());
	for (size_t i = 0; i < visibleMarkerCount; ++i) {
		TBBUTTON button{};
		button.iBitmap = imageIndices[i];
		button.idCommand = WORDMARKER_BUTTON_BASE + static_cast<UINT>(i);
		button.fsState = TBSTATE_ENABLED | (entries[i].enabled ? TBSTATE_CHECKED : 0);
		button.fsStyle = BTNS_CHECK | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
		button.iString = firstString + static_cast<int>(i);
		buttons.push_back(button);
	}
	size_t labelIndex = visibleMarkerCount;
	if (hasAutomatic) {
		TBBUTTON button{};
		button.iBitmap = autoImageIndex;
		button.idCommand = WORDMARKER_AUTO_BUTTON;
		button.fsState = TBSTATE_ENABLED;
		button.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
		button.iString = firstString + static_cast<int>(labelIndex++);
		buttons.push_back(button);
	}
	TBBUTTON clearButton{};
	clearButton.iBitmap = I_IMAGENONE;
	clearButton.idCommand = WORDMARKER_CLEAR_BUTTON;
	clearButton.fsState = TBSTATE_ENABLED;
	clearButton.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
	clearButton.iString = firstString + static_cast<int>(labelIndex++);
	buttons.push_back(clearButton);
	TBBUTTON overflowButton{};
	overflowButton.iBitmap = I_IMAGENONE;
	overflowButton.idCommand = WORDMARKER_OVERFLOW_BUTTON;
	overflowButton.fsState = TBSTATE_ENABLED;
	overflowButton.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
	overflowButton.iString = firstString + static_cast<int>(labelIndex);
	buttons.push_back(overflowButton);
	if (!buttons.empty()) ::SendMessage(m_hwndEnhancementToolBar, TB_ADDBUTTONS,
		static_cast<WPARAM>(buttons.size()), reinterpret_cast<LPARAM>(buttons.data()));
	::SendMessage(m_hwndEnhancementToolBar, TB_AUTOSIZE, 0, 0);

	m_markerButtonWords.clear();
	for (const auto& entry : entries) m_markerButtonWords.push_back(entry.word);
	m_markerButtonBackColors = std::move(markerColors);
	m_markerButtonEnabled = std::move(markerEnabled);
	m_markerAutoColor = autoColor;
	m_markerAutoWord = autoWord;
	m_markerAutoButtonVisible = hasAutomatic;
	LayoutMarkerControls();
}

void CMainToolBar::RefreshCurlSyntaxControl()
{
	if (!m_hwndToolBar || !::IsWindow(m_hwndToolBar)) return;
	if (!::IsWindow(m_hwndEnhancementToolBar)) {
		const LONG_PTR nativeStyle = ::GetWindowLongPtr(m_hwndToolBar, GWL_STYLE);
		const DWORD style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_LIST | TBSTYLE_TOOLTIPS |
			CCS_NORESIZE | CCS_NOPARENTALIGN | CCS_NODIVIDER | (nativeStyle & TBSTYLE_FLAT);
		m_hwndEnhancementToolBar = ::CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr, style,
			0, 0, 0, 0, m_pOwner->GetHwnd(), reinterpret_cast<HMENU>(static_cast<INT_PTR>(ENHANCEMENT_TOOLBAR_ID)),
			CEditApp::getInstance()->GetAppInstance(), nullptr);
		if (!m_hwndEnhancementToolBar) return;
		::SendMessage(m_hwndEnhancementToolBar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
		::SendMessage(m_hwndEnhancementToolBar, TB_SETBITMAPSIZE, 0,
			MAKELONG(DpiScaleX(16), DpiScaleY(16)));
		::SendMessage(m_hwndEnhancementToolBar, TB_SETBUTTONSIZE, 0,
			MAKELONG(DpiScaleX(22), DpiScaleY(22)));
		::SendMessage(m_hwndEnhancementToolBar, TB_SETEXTENDEDSTYLE,
			TBSTYLE_EX_MIXEDBUTTONS, TBSTYLE_EX_MIXEDBUTTONS);
		::SetWindowSubclass(m_hwndEnhancementToolBar, &ToolBarWndProc, 3, reinterpret_cast<DWORD_PTR>(this));
		DarkMode::setDarkWndSafe(m_hwndEnhancementToolBar);

		std::wstring labels = L"構文チェック";
		labels.push_back(L'\0');
		labels.append(L"前へ");
		labels.push_back(L'\0');
		labels.append(L"次へ");
		labels.push_back(L'\0');
		labels.push_back(L'\0');
		const int stringBase = static_cast<int>(::SendMessageW(m_hwndEnhancementToolBar,
			TB_ADDSTRINGW, 0, reinterpret_cast<LPARAM>(labels.c_str())));
		TBBUTTON buttons[7]{};
		buttons[0].iBitmap = DpiScaleX(3);
		buttons[0].fsStyle = BTNS_SEP;
		buttons[1].iBitmap = I_IMAGENONE;
		buttons[1].idCommand = CURL_SYNTAX_BUTTON;
		buttons[1].fsState = TBSTATE_ENABLED;
		buttons[1].fsStyle = BTNS_CHECK | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
		buttons[1].iString = stringBase;
		buttons[2].iBitmap = DpiScaleX(3);
		buttons[2].fsStyle = BTNS_SEP;
		buttons[3].iBitmap = 0;
		buttons[3].idCommand = WORDMARKER_PREVIOUS_BUTTON;
		buttons[3].fsState = 0;
		buttons[3].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
		buttons[3].iString = stringBase + 1;
		buttons[4].iBitmap = 1;
		buttons[4].idCommand = WORDMARKER_NEXT_BUTTON;
		buttons[4].fsState = 0;
		buttons[4].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
		buttons[4].iString = stringBase + 2;
		buttons[5].iBitmap = DpiScaleX(3);
		buttons[5].fsStyle = BTNS_SEP;
		buttons[6].iBitmap = DpiScaleX(1);
		buttons[6].idCommand = WORDMARKER_SPACER_BUTTON;
		buttons[6].fsStyle = BTNS_SEP;
		::SendMessage(m_hwndEnhancementToolBar, TB_ADDBUTTONS, 7, reinterpret_cast<LPARAM>(buttons));
		m_nEnhancementMarkerStart = 7;
		::SendMessage(m_hwndEnhancementToolBar, TB_AUTOSIZE, 0, 0);
	}
	if (!m_hwndEnhancementToolBar) return;
	const bool supported = m_pOwner->IsCurlSyntaxDocument();
	const bool enabled = curlsyntax::IsRealtimeEnabled();
	if (!m_bCurlSyntaxControlInitialized || supported != m_bCurlSyntaxSupported || enabled != m_bCurlSyntaxEnabled) {
		m_bCurlSyntaxControlInitialized = true;
		m_bCurlSyntaxSupported = supported;
		m_bCurlSyntaxEnabled = enabled;
		::SendMessage(m_hwndEnhancementToolBar, TB_ENABLEBUTTON, CURL_SYNTAX_BUTTON, TRUE);
		::SendMessage(m_hwndEnhancementToolBar, TB_CHECKBUTTON, CURL_SYNTAX_BUTTON, MAKELONG(enabled, 0));
	}
	LayoutMarkerControls();
}

bool CMainToolBar::GetEnhancementTooltipText(HWND hwndTooltip, UINT_PTR idFrom, WCHAR* buffer, size_t bufferSize) const
{
	if (!buffer || bufferSize == 0 || !::IsWindow(m_hwndEnhancementToolBar) ||
		::SendMessage(m_hwndEnhancementToolBar, TB_GETTOOLTIPS, 0, 0) != reinterpret_cast<LRESULT>(hwndTooltip)) return false;
	std::wstring text;
	const UINT id = static_cast<UINT>(idFrom);
	if (id == CURL_SYNTAX_BUTTON) {
		text = L"Curl 構文チェックの切替";
	} else if (id == WORDMARKER_PREVIOUS_BUTTON) {
		text = L"前へ：前の自動 Marker に移動";
	} else if (id == WORDMARKER_NEXT_BUTTON) {
		text = L"次へ：次の自動 Marker に移動";
	} else if (id >= WORDMARKER_BUTTON_BASE && id < WORDMARKER_AUTO_BUTTON) {
		const size_t index = id - WORDMARKER_BUTTON_BASE;
		text = L"固定 Marker";
		if (index < m_markerButtonWords.size()) text += L"「" + m_markerButtonWords[index] + L"」";
		text += L"の表示／非表示（右クリックで色変更）";
	} else if (id == WORDMARKER_AUTO_BUTTON) {
		text = L"自動 Marker を解除";
	} else if (id == WORDMARKER_CLEAR_BUTTON) {
		text = L"固定・自動 Marker をすべて解除";
	} else if (id == WORDMARKER_OVERFLOW_BUTTON) {
		text = L"Marker 一覧を表示";
	} else {
		return false;
	}
	wcsncpy_s(buffer, bufferSize, text.c_str(), _TRUNCATE);
	return true;
}

void CMainToolBar::LayoutMarkerControls()
{
	HWND parent = m_pOwner ? m_pOwner->GetHwnd() : nullptr;
	if (!m_hwndToolBar || !::IsWindow(m_hwndToolBar) || !::IsWindow(m_hwndEnhancementToolBar) ||
		!parent || !::IsWindow(parent)) return;
	RECT client{};
	::GetClientRect(parent, &client);
	RECT toolbar{};
	HWND toolbarRow = m_hwndReBar ? m_hwndReBar : m_hwndToolBar;
	::GetWindowRect(toolbarRow, &toolbar);
	::MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&toolbar), 2);
	int leftLimit = 0;
	const int nativeCount = static_cast<int>(::SendMessage(m_hwndToolBar, TB_BUTTONCOUNT, 0, 0));
	for (int index = nativeCount - 1; index >= 0; --index) {
		RECT item{};
		if (::SendMessage(m_hwndToolBar, TB_GETITEMRECT, index, reinterpret_cast<LPARAM>(&item))) {
			::MapWindowPoints(m_hwndToolBar, parent, reinterpret_cast<POINT*>(&item), 2);
			leftLimit = item.right + DpiScaleX(4);
			break;
		}
	}
	const int height = std::max(DpiScaleY(22), toolbar.bottom - toolbar.top);
	if (height <= 0 || leftLimit >= client.right) {
		::ShowWindow(m_hwndEnhancementToolBar, SW_HIDE);
		return;
	}
	const int width = client.right - leftLimit;
	const int y = toolbar.top + (toolbar.bottom - toolbar.top - height) / 2;
	::ShowWindow(m_hwndEnhancementToolBar, SW_SHOW);
	::SetWindowPos(m_hwndEnhancementToolBar, HWND_TOP, leftLimit, y, width, height, SWP_NOACTIVATE);

	// Resize the native toolbar separator so the Marker group sits at the far right.
	TBBUTTONINFO spacer{};
	spacer.cbSize = sizeof(spacer);
	spacer.dwMask = TBIF_SIZE;
	spacer.cx = static_cast<WORD>(DpiScaleX(1));
	::SendMessage(m_hwndEnhancementToolBar, TB_SETBUTTONINFO, WORDMARKER_SPACER_BUTTON, reinterpret_cast<LPARAM>(&spacer));
	::SendMessage(m_hwndEnhancementToolBar, TB_AUTOSIZE, 0, 0);
	const int count = static_cast<int>(::SendMessage(m_hwndEnhancementToolBar, TB_BUTTONCOUNT, 0, 0));
	if (count > 0) {
		RECT last{};
		RECT sep{};
		if (::SendMessage(m_hwndEnhancementToolBar, TB_GETITEMRECT, count - 1, reinterpret_cast<LPARAM>(&last)) &&
			::SendMessage(m_hwndEnhancementToolBar, TB_GETITEMRECT, m_nEnhancementMarkerStart - 1, reinterpret_cast<LPARAM>(&sep))) {
			const int sepWidth = sep.right - sep.left;
			spacer.cx = static_cast<WORD>(std::clamp<LONG>(width - last.right + sepWidth - DpiScaleX(2), DpiScaleX(1), 60000));
			::SendMessage(m_hwndEnhancementToolBar, TB_SETBUTTONINFO, WORDMARKER_SPACER_BUTTON, reinterpret_cast<LPARAM>(&spacer));
		}
	}
	::SendMessage(m_hwndEnhancementToolBar, TB_AUTOSIZE, 0, 0);
	::SetWindowPos(m_hwndEnhancementToolBar, HWND_TOP, leftLimit, y, width, height, SWP_NOACTIVATE);
}

void CMainToolBar::HandleMarkerColorChange(UINT controlId)
{
	auto& markers = CWordMarker::Instance();
	if (controlId < WORDMARKER_BUTTON_BASE || controlId >= WORDMARKER_AUTO_BUTTON) return;
	const size_t markerIndex = controlId - WORDMARKER_BUTTON_BASE;
	if (markerIndex >= markers.Entries().size()) return;
	const auto& colors = m_pOwner->GetActiveView().GetDocument()->m_cDocType.GetDocumentAttribute().m_ColorInfoArr;
	std::vector<unsigned> palette;
	for (int index = COLORIDX_SEARCH + 1; index <= COLORIDX_SEARCHTAIL; ++index) {
		const auto& attr = colors[index].m_sColorAttr;
		if (attr.m_cTEXT != attr.m_cBACK) palette.push_back(static_cast<unsigned>(index - COLORIDX_SEARCH));
	}
	if (palette.empty()) return;
	const unsigned current = markers.Entries()[markerIndex].color;
	const auto found = std::find(palette.begin(), palette.end(), current);
	const size_t next = found == palette.end() ? 0 : (static_cast<size_t>(found - palette.begin()) + 1) % palette.size();
	markers.SetColor(markerIndex, palette[next]);
	m_pOwner->GetActiveView().Redraw();
	RefreshMarkerControls();
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
			const std::wstring autoMenuLabel = m_markerAutoButtonVisible
				? L"自動 Marker を解除: " + m_markerAutoWord : L"自動 Marker はありません";
			::AppendMenuW(menu, MF_STRING | (m_markerAutoButtonVisible ? MF_CHECKED : MF_UNCHECKED), WORDMARKER_MENU_AUTO,
				autoMenuLabel.c_str());
			::EnableMenuItem(menu, WORDMARKER_MENU_AUTO, MF_BYCOMMAND | (m_markerAutoButtonVisible ? MF_ENABLED : MF_GRAYED));
			::AppendMenuW(menu, MF_STRING, WORDMARKER_MENU_PREVIOUS, L"自動 Marker の前へ");
			::AppendMenuW(menu, MF_STRING, WORDMARKER_MENU_NEXT, L"自動 Marker の次へ");
			::EnableMenuItem(menu, WORDMARKER_MENU_PREVIOUS, MF_BYCOMMAND | (m_markerAutoButtonVisible ? MF_ENABLED : MF_GRAYED));
			::EnableMenuItem(menu, WORDMARKER_MENU_NEXT, MF_BYCOMMAND | (m_markerAutoButtonVisible ? MF_ENABLED : MF_GRAYED));
			::AppendMenuW(menu, MF_STRING, WORDMARKER_MENU_CLEAR, L"Marker をすべて解除");
			RECT rc{};
			::SendMessage(m_hwndEnhancementToolBar, TB_GETRECT, WORDMARKER_OVERFLOW_BUTTON, reinterpret_cast<LPARAM>(&rc));
			::MapWindowPoints(m_hwndEnhancementToolBar, nullptr, reinterpret_cast<POINT*>(&rc), 2);
			const UINT selected = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, rc.left, rc.bottom, 0, m_pOwner->GetHwnd(), nullptr);
			::DestroyMenu(menu);
			if (selected >= WORDMARKER_MENU_REMOVE_BASE && selected < WORDMARKER_MENU_REMOVE_BASE + 32) {
				const size_t index = selected - WORDMARKER_MENU_REMOVE_BASE;
				if (index < markers.Entries().size()) markers.ToggleEnabled(index);
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
		if (index < markers.Entries().size()) markers.ToggleEnabled(index);
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
