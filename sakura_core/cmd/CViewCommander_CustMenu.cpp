/*!	@file
@brief CViewCommanderクラスのコマンド(カスタムメニュー)関数群

	2012/12/20	CViewCommander.cppから分離
*/
/*
	Copyright (C) 1998-2001, Norio Nakatani
	Copyright (C) 2000-2001, genta
	Copyright (C) 2002, YAZAKI, genta
	Copyright (C) 2006, fon
	Copyright (C) 2007, ryoji, maru, Uchi
	Copyright (C) 2008, ryoji, nasukoji
	Copyright (C) 2009, ryoji, nasukoji
	Copyright (C) 2018-2022, Sakura Editor Organization

	This source code is designed for sakura editor.
	Please contact the copyright holders to use this code for other purpose.
*/

#include "StdAfx.h"
#include <algorithm>
#include "CViewCommander.h"
#include "CViewCommander_inline.h"

#include "apiwrap/StdApi.h"
#include "view/colors/CWordMarker.h"

/* 右クリックメニュー */
void CViewCommander::Command_MENU_RBUTTON( void )
{
	/* ポップアップメニュー(右クリック) */
	auto nId = m_pCommanderView->CreatePopUpMenu_R();
	if( 0 == nId ){
		return;
	}
	switch( nId ){
	case IDM_COPYDICINFO:
	{
		CNativeW cInfo = m_pCommanderView->m_cTipWnd.GetInfoText();

		// 貼り付けで扱いやすいように、改行記号を CR+LF に置換する
		cInfo.Replace( L"\n", L"\r\n" );

		/* クリップボードにデータを設定 */
		m_pCommanderView->MySetClipboardData( cInfo.GetStringPtr(), cInfo.GetStringLength(), false );

		break;
	}
	case IDM_JUMPDICT:
		/* キーワード辞書ファイルを開く */
		if(m_pCommanderView->m_pTypeData->m_bUseKeyWordHelp){		/* キーワード辞書セレクトを使用する */	// 2006.04.10 fon
			//	Feb. 17, 2007 genta 相対パスを実行ファイル基準で開くように
			m_pCommanderView->TagJumpSub(
				m_pCommanderView->m_pTypeData->m_KeyHelpArr[m_pCommanderView->m_cTipWnd.m_nSearchDict].m_szPath,
				CMyPoint(1, m_pCommanderView->m_cTipWnd.m_nSearchLine),
				0,
				true
			);
		}
		break;
	case IDM_WORDMARKER_TOGGLE:
	{
		CNativeW selected;
		auto& markers = CWordMarker::Instance();
		if (m_pCommanderView->GetSelectionInfo().IsTextSelected() &&
			m_pCommanderView->GetSelectedDataSimple(selected) && selected.GetStringLength() > 0) {
			const auto& colors = GetDocument()->m_cDocType.GetDocumentAttribute().m_ColorInfoArr;
			std::vector<unsigned> palette;
			for (int i = COLORIDX_SEARCH + 1; i <= COLORIDX_SEARCHTAIL; ++i) {
				const auto& attr = colors[i].m_sColorAttr;
				if (attr.m_cTEXT != attr.m_cBACK) palette.push_back(static_cast<unsigned>(i - COLORIDX_SEARCH));
			}
			const auto color = markers.NextColor(palette);
			markers.Toggle(std::wstring(selected.GetStringPtr(), selected.GetStringLength()), color);
			m_pCommanderView->Redraw();
		}
		break;
	}
	case IDM_WORDMARKER_CLEAR:
		CWordMarker::Instance().ClearFixed();
		m_pCommanderView->Redraw();
		break;

	default:
		if (nId >= IDM_WORDMARKER_REMOVE_BASE && nId < IDM_WORDMARKER_REMOVE_BASE + 32) {
			auto& markers = CWordMarker::Instance();
			const size_t index = nId - IDM_WORDMARKER_REMOVE_BASE;
			if (index < markers.Entries().size()) {
				const auto entry = markers.Entries()[index];
				markers.Toggle(entry.word, entry.color);
				m_pCommanderView->Redraw();
			}
			break;
		}
		if (nId >= IDM_WORDMARKER_COLOR_BASE && nId < IDM_WORDMARKER_COLOR_BASE + 32) {
			auto& markers = CWordMarker::Instance();
			const size_t index = nId - IDM_WORDMARKER_COLOR_BASE;
			if (index < markers.Entries().size()) {
				const auto& colors = GetDocument()->m_cDocType.GetDocumentAttribute().m_ColorInfoArr;
				std::vector<unsigned> palette;
				for (int i = COLORIDX_SEARCH + 1; i <= COLORIDX_SEARCHTAIL; ++i) {
					const auto& attr = colors[i].m_sColorAttr;
					const auto slot = static_cast<unsigned>(i - COLORIDX_SEARCH);
					if (attr.m_cTEXT != attr.m_cBACK && std::find(palette.begin(), palette.end(), slot) == palette.end()) palette.push_back(slot);
				}
				if (!palette.empty()) {
					const auto current = markers.Entries()[index].color;
					const auto found = std::find(palette.begin(), palette.end(), current);
					const size_t next = found == palette.end() ? 0 : (static_cast<size_t>(found - palette.begin()) + 1) % palette.size();
					markers.SetColor(index, palette[next]);
					m_pCommanderView->Redraw();
				}
			}
			break;
		}
		/* コマンドコードによる処理振り分け */
		::PostMessageCmd( GetMainWindow(), WM_COMMAND, MAKELONG( nId, 0 ),  (LPARAM)nullptr );
		break;
	}
	GetEditWindow()->UpdateMarkerToolbar();
	return;
}

/* カスタムメニュー表示 */
int CViewCommander::Command_CUSTMENU( int nMenuIdx )
{
	HMENU		hMenu;

	GetEditWindow()->GetMenuDrawer().ResetContents();

	//	Oct. 3, 2001 genta

	if( nMenuIdx < 0 || MAX_CUSTOM_MENU <= nMenuIdx ){
		return 0;
	}
	if( 0 == GetDllShareData().m_Common.m_sCustomMenu.m_nCustMenuItemNumArr[nMenuIdx] ){
		return 0;
	}
	hMenu = ::CreatePopupMenu();
	return m_pCommanderView->CreatePopUpMenuSub( hMenu, nMenuIdx, nullptr, KEYHELP_RMENU_NONE );
}
