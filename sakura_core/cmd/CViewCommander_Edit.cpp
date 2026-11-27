/*!	@file
@brief CViewCommanderクラスのコマンド(編集系 基本形)関数群

	2012/12/16	CViewCommander.cpp,CViewCommander_New.cppから分離
*/
/*
	Copyright (C) 1998-2001, Norio Nakatani
	Copyright (C) 2002, genta
	Copyright (C) 2003, MIK, genta, かろと, zenryaku, Moca, ryoji, naoh, KEITA, じゅうじ
	Copyright (C) 2005, genta, D.S.Koba, ryoji
	Copyright (C) 2007, ryoji, kobake
	Copyright (C) 2008, ryoji, nasukoji
	Copyright (C) 2009, ryoji
	Copyright (C) 2010, ryoji
	Copyright (C) 2018-2022, Sakura Editor Organization

	This source code is designed for sakura editor.
	Please contact the copyright holders to use this code for other purpose.
*/

#include "StdAfx.h"
#include "CViewCommander.h"
#include "CViewCommander_inline.h"

#include "view/CRuler.h"
#include "uiparts/CWaitCursor.h"
#include "plugin/CJackManager.h"
#include "plugin/CSmartIndentIfObj.h"
#include "debug/CRunningTimer.h"
#include "view/MultiCursorPointTransform.h"
#include "view/MultiCursorUndoState.h"
#include "window/EnhancementToolbarSettings.h"
#include <algorithm>
#include <array>
#include <string_view>
#include <vector>
#include "apiwrap/CommonControl.h"

namespace {
	using MultiCursorUndoPositions = std::array<multicursor::UndoPosition<CLogicPoint>, CEditView::MAX_MULTI_CURSORS>;
	void SetMultiCursorSelection(CEditView& view, const CLogicPoint& caret,
		const CLogicPoint& anchor, bool selected);

	MultiCursorUndoPositions CaptureMultiCursorUndoPositions(COpeBlk* opeBlk, bool redo)
	{
		MultiCursorUndoPositions positions{};
		if (!opeBlk) return positions;
		for (int i = 0; i < opeBlk->GetNum(); ++i) {
			COpe* ope = opeBlk->GetOpe(i);
			if (!ope || ope->m_nMultiCursorSlot < 0
				|| static_cast<std::size_t>(ope->m_nMultiCursorSlot) >= positions.size()) continue;
			CLogicPoint anchorBefore = ope->m_ptCaretPos_PHY_Before;
			CLogicPoint anchorAfter = ope->m_ptCaretPos_PHY_After;
			bool selectedBefore = false;
			bool selectedAfter = false;
			bool stateMarker = false;
			if (ope->m_bMultiCursorStateMarker) {
				const auto* cursorOpe = static_cast<const CMultiCursorStateOpe*>(ope);
				anchorBefore = cursorOpe->m_ptAnchorBefore;
				anchorAfter = cursorOpe->m_ptAnchorAfter;
				selectedBefore = cursorOpe->m_bHasSelectionBefore;
				selectedAfter = cursorOpe->m_bHasSelectionAfter;
				stateMarker = cursorOpe->m_bMultiCursorStateMarker;
			}
			multicursor::RecordUndoPosition(positions, ope->m_nMultiCursorSlot,
				ope->m_ptCaretPos_PHY_Before, ope->m_ptCaretPos_PHY_After,
				anchorBefore, anchorAfter, selectedBefore, selectedAfter, stateMarker, redo);
		}
		return positions;
	}

	void RestoreMultiCursorUndoPositions(CEditView& view, const MultiCursorUndoPositions& positions)
	{
		const bool hasTaggedPosition = std::any_of(positions.begin(), positions.end(),
			[](const auto& slot) { return slot.valid; });
		if (!hasTaggedPosition) {
			view.ClearExtraCursors();
			return;
		}

		if (positions[0].valid) {
			SetMultiCursorSelection(view, positions[0].caret, positions[0].anchor, positions[0].hasSelection);
			view.GetCaret().m_nCaretPosX_Prev = view.GetCaret().GetCaretLayoutPos().GetX2();
		}
		view.m_vExtraCursors.clear();
		for (std::size_t slot = 1; slot < positions.size(); ++slot) {
			if (!positions[slot].valid) continue;
			CEditView::SExtraCursor extra;
			extra.caret = positions[slot].caret;
			extra.anchor = positions[slot].anchor;
			extra.bHasSelection = positions[slot].hasSelection;
			view.m_vExtraCursors.push_back(extra);
		}
		view.Redraw();
	}
}

/* wchar_t1個分の文字を入力 */
void CViewCommander::Command_WCHAR( wchar_t wcChar, bool bConvertEOL )
{
	if( m_pCommanderView->GetSelectionInfo().IsMouseSelecting() ){	/* マウスによる範囲選択中 */
		ErrorBeep();
		return;
	}

	CLogicInt		nPos;
	CLogicInt		nCharChars;

	GetDocument()->m_cDocEditor.SetModified(true,true);	//	Jan. 22, 2002 genta

	if( m_pCommanderView->m_bHideMouse && 0 <= m_pCommanderView->m_nMousePause ){
		m_pCommanderView->m_nMousePause = -1;
		::SetCursor( nullptr );
	}

	/* 現在位置にデータを挿入 */
	CNativeW cmemDataW2(&wcChar, 1);
	if( WCODE::IsLineDelimiter(wcChar, GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol) ){ 
		/* 現在、Enterなどで挿入する改行コードの種類を取得 */
		if( bConvertEOL ){
			CEol cWork = GetDocument()->m_cDocEditor.GetNewLineCode();
			cmemDataW2.SetString( cWork.GetValue2(), cWork.GetLen() );
		}

		/* テキストが選択されているか */
		if( m_pCommanderView->GetSelectionInfo().IsTextSelected() ){
			m_pCommanderView->DeleteData( true );
		}
		if( m_pCommanderView->m_pTypeData->m_bAutoIndent ){	/* オートインデント */
			const CLayout* pCLayout;
			const wchar_t*	pLine;
			CLogicInt		nLineLen;
			pLine = GetDocument()->m_cLayoutMgr.GetLineStr( GetCaret().GetCaretLayoutPos().GetY2(), &nLineLen, &pCLayout );
			if( nullptr != pCLayout ){
				const CDocLine* pcDocLine;
				pcDocLine = GetDocument()->m_cDocLineMgr.GetLine( pCLayout->GetLogicLineNo() );
				pLine = pcDocLine->GetDocLineStrWithEOL( &nLineLen );
				if( nullptr != pLine ){
					/*
					  カーソル位置変換
					  レイアウト位置(行頭からの表示桁位置、折り返しあり行位置)
					  →
					  物理位置(行頭からのバイト数、折り返し無し行位置)
					*/
					CLogicPoint ptXY;
					GetDocument()->m_cLayoutMgr.LayoutToLogic(
						GetCaret().GetCaretLayoutPos(),
						&ptXY
					);

					/* 指定された桁に対応する行のデータ内の位置を調べる */
					for( nPos = CLogicInt(0); nPos < nLineLen && nPos < ptXY.GetX2(); ){
						// 2005-09-02 D.S.Koba GetSizeOfChar
						nCharChars = CNativeW::GetSizeOfChar( pLine, nLineLen, nPos );

						/* その他のインデント文字 */
						if( 0 < nCharChars
						 && pLine[nPos] != L'\0'	// その他のインデント文字に L'\0' は含まれない	// 2009.02.04 ryoji L'\0'がインデントされてしまう問題修正
						 && m_pCommanderView->m_pTypeData->m_szIndentChars[0] != L'\0'
						){
							wchar_t szCurrent[10];
							wmemcpy( szCurrent, &pLine[nPos], nCharChars );
							szCurrent[nCharChars] = L'\0';
							/* その他のインデント対象文字 */
							if( nullptr != wcsstr(
								m_pCommanderView->m_pTypeData->m_szIndentChars,
								szCurrent
							) ){
								goto end_of_for;
							}
						}
						
						{
							bool bZenSpace=m_pCommanderView->m_pTypeData->m_bAutoIndent_ZENSPACE;
							if(nCharChars==1 && WCODE::IsIndentChar(pLine[nPos],bZenSpace))
							{
								//下へ進む
							}
							else break;
						}

end_of_for:;
						nPos += nCharChars;
					}

					//インデント取得
					//CNativeW cmemIndent;
					//cmemIndent.SetString( pLine, nPos );

					//インデント付加
					cmemDataW2.AppendString(pLine, nPos);
				}
			}
		}
	}
	else{
		/* テキストが選択されているか */
		if( m_pCommanderView->GetSelectionInfo().IsTextSelected() ){
			/* 矩形範囲選択中か */
			if( m_pCommanderView->GetSelectionInfo().IsBoxSelecting() ){
				Command_INDENT( wcChar );
				return;
			}else{
				m_pCommanderView->DeleteData( true );
			}
		}
		else{
			if( ! m_pCommanderView->IsInsMode() /* Oct. 2, 2005 genta */ ){
				DelCharForOverwrite(&wcChar, 1);	// 上書き用の一文字削除	// 2009.04.11 ryoji
			}
		}
	}

	//本文に挿入する
	CLayoutPoint ptLayoutNew;
	m_pCommanderView->InsertData_CEditView(
		GetCaret().GetCaretLayoutPos(),
		cmemDataW2.GetStringPtr(),
		cmemDataW2.GetStringLength(),
		&ptLayoutNew,
		true
	);

	/* 挿入データの最後へカーソルを移動 */
	GetCaret().MoveCursor( ptLayoutNew, true );
	GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();

	/* スマートインデント */
	ESmartIndentType nSIndentType = m_pCommanderView->m_pTypeData->m_eSmartIndent;
	switch( nSIndentType ){	/* スマートインデント種別 */
	case SMARTINDENT_NONE:
		break;
	case SMARTINDENT_CPP:
		/* C/C++スマートインデント処理 */
		m_pCommanderView->SmartIndent_CPP( wcChar );
		break;
	default:
		//プラグインから検索する
		{
			CPlug::Array plugs;
			CJackManager::getInstance()->GetUsablePlug( PP_SMARTINDENT, nSIndentType, &plugs );

			if( plugs.size() > 0 ){
				assert_warning( 1 == plugs.size() );
				//インターフェースオブジェクト準備
				CWSHIfObj::List params;
				CSmartIndentIfObj* objIndent = new CSmartIndentIfObj( wcChar );	//スマートインデントオブジェクト
				objIndent->AddRef();
				params.push_back( objIndent );

				//キー入力をアンドゥバッファに反映
				m_pCommanderView->SetUndoBuffer();

				//キー入力とは別の操作ブロックにする（ただしプラグイン内の操作はまとめる）
				if( GetOpeBlk() == nullptr ){
					SetOpeBlk(new COpeBlk);
				}
				GetOpeBlk()->AddRef();	// ※ReleaseはHandleCommandの最後で行う

				//プラグイン呼び出し
				( *plugs.begin() )->Invoke( m_pCommanderView, params );
				objIndent->Release();
			}
		}
		break;
	}

	/* 2005.10.11 ryoji 改行時に末尾の空白を削除 */
	if( WCODE::IsLineDelimiter(wcChar, GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol) && m_pCommanderView->m_pTypeData->m_bRTrimPrevLine ){	/* 改行時に末尾の空白を削除 */
		/* 前の行にある末尾の空白を削除する */
		m_pCommanderView->RTrimPrevLine();
	}

	m_pCommanderView->PostprocessCommand_hokan();	//	Jan. 10, 2005 genta 関数化
}

/*!
	@brief 2バイト文字入力
	
	WM_IME_CHARで送られてきた文字を処理する．
	ただし，挿入モードではWM_IME_CHARではなくWM_IME_COMPOSITIONで文字列を
	取得するのでここには来ない．

	@param wChar [in] SJIS漢字コード．上位が1バイト目，下位が2バイト目．
	
	@date 2002.10.06 genta 引数の上下バイトの意味を逆転．
		WM_IME_CHARのwParamに合わせた．
*/
void CViewCommander::Command_IME_CHAR( WORD wChar )
{
	if( m_pCommanderView->GetSelectionInfo().IsMouseSelecting() ){	/* マウスによる範囲選択中 */
		ErrorBeep();
		return;
	}

	//	Oct. 6 ,2002 genta 上下逆転
	if( 0 == (wChar & 0xff00) ){
		Command_WCHAR( wChar & 0xff );
		return;
	}
	GetDocument()->m_cDocEditor.SetModified(true,true);	//	Jan. 22, 2002 genta

	if( m_pCommanderView->m_bHideMouse && 0 <= m_pCommanderView->m_nMousePause ){
		m_pCommanderView->m_nMousePause = -1;
		::SetCursor( nullptr );
	}

	// Oct. 6 ,2002 genta バッファに格納する
	// Aug. 15, 2007 kobake WCHARバッファに変換する
	wchar_t szWord[2]={wChar,0};
	CLogicInt nWord=CLogicInt(1);

	/* テキストが選択されているか */
	if( m_pCommanderView->GetSelectionInfo().IsTextSelected() ){
		/* 矩形範囲選択中か */
		if( m_pCommanderView->GetSelectionInfo().IsBoxSelecting() ){
			Command_INDENT( szWord, nWord );	//	Oct. 6 ,2002 genta 
			return;
		}else{
			m_pCommanderView->DeleteData( true );
		}
	}
	else{
		if( ! m_pCommanderView->IsInsMode() /* Oct. 2, 2005 genta */ ){
			DelCharForOverwrite(szWord, nWord);	// 上書き用の一文字削除	// 2009.04.11 ryoji
		}
	}

	//	Oct. 6 ,2002 genta 
	CLayoutPoint ptLayoutNew;
	m_pCommanderView->InsertData_CEditView( GetCaret().GetCaretLayoutPos(), szWord, nWord, &ptLayoutNew, true );

	/* 挿入データの最後へカーソルを移動 */
	GetCaret().MoveCursor( ptLayoutNew, true );
	GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();

	m_pCommanderView->PostprocessCommand_hokan();	//	Jan. 10, 2005 genta 関数化
}

//	from CViewCommander_New.cpp
/* Undo 元に戻す */
void CViewCommander::Command_UNDO( void )
{
	if( m_pCommanderView->GetSelectionInfo().IsMouseSelecting() ){	/* マウスによる範囲選択中 */
		ErrorBeep();
		return;
	}

	{
		COpeBlk* opeBlk = m_pCommanderView->m_cCommander.GetOpeBlk();
		if( opeBlk ){
			int nCount = opeBlk->GetRefCount();
			opeBlk->SetRefCount(1); // 強制的にリセットするため1を指定
			m_pCommanderView->SetUndoBuffer();
			if( m_pCommanderView->m_cCommander.GetOpeBlk() == nullptr && 0 < nCount ){
				m_pCommanderView->m_cCommander.SetOpeBlk(new COpeBlk());
				m_pCommanderView->m_cCommander.GetOpeBlk()->SetRefCount( nCount );
			}
		}
	}

	if( !GetDocument()->m_cDocEditor.IsEnableUndo() ){	/* Undo(元に戻す)可能な状態か？ */
		return;
	}

	MY_RUNNINGTIMER( cRunningTimer, L"CViewCommander::Command_UNDO()" );

	COpe*		pcOpe = nullptr;

	COpeBlk*	pcOpeBlk;
	int			nOpeBlkNum;
	int			i;
	bool		bIsModified;
	MultiCursorUndoPositions multiCursorPositions{};
//	int			nNewLine;	/* 挿入された部分の次の位置の行 */
//	int			nNewPos;	/* 挿入された部分の次の位置のデータ位置 */

	CLayoutPoint ptCaretPos_Before;

	CLayoutPoint ptCaretPos_After;

	/* 各種モードの取り消し */
	Command_CANCEL_MODE();
	// Keep stale post-edit positions out of redraws; tagged OPE records rebuild them after replay.
	m_pCommanderView->ClearExtraCursors();

	m_pCommanderView->m_bDoing_UndoRedo = true;	/* アンドゥ・リドゥの実行中か */

	/* 現在のUndo対象の操作ブロックを返す */
	if( nullptr != ( pcOpeBlk = GetDocument()->m_cDocEditor.m_cOpeBuf.DoUndo( &bIsModified ) ) ){
		multiCursorPositions = CaptureMultiCursorUndoPositions(pcOpeBlk, false);
		nOpeBlkNum = pcOpeBlk->GetNum();
		bool bDraw = (nOpeBlkNum < 5) && m_pCommanderView->GetDrawSwitch();
		bool bDrawAll = false;
		const bool bDrawSwitchOld = m_pCommanderView->SetDrawSwitch(bDraw);	// hor

		CWaitCursor cWaitCursor( m_pCommanderView->GetHwnd(), 1000 < nOpeBlkNum );
		HWND hwndProgress = nullptr;
		int nProgressPos = 0;
		if( cWaitCursor.IsEnable() ){
			hwndProgress = m_pCommanderView->StartProgress();
		}

		const bool bFastMode = (100 < nOpeBlkNum);
		for( i = nOpeBlkNum - 1; i >= 0; i-- ){
			pcOpe = pcOpeBlk->GetOpe( i );
			if( bFastMode ){
				GetCaret().MoveCursorFastMode( pcOpe->m_ptCaretPos_PHY_After );
			}else{
				GetDocument()->m_cLayoutMgr.LogicToLayout(
					pcOpe->m_ptCaretPos_PHY_After,
					&ptCaretPos_After
				);
				GetDocument()->m_cLayoutMgr.LogicToLayout(
					pcOpe->m_ptCaretPos_PHY_Before,
					&ptCaretPos_Before
				);

				/* カーソルを移動 */
				GetCaret().MoveCursor( ptCaretPos_After, false );
			}

			switch( pcOpe->GetCode() ){
			case OPE_INSERT:
				{
					CInsertOpe* pcInsertOpe = static_cast<CInsertOpe*>(pcOpe);

					/* 選択範囲の変更 */
					CLogicRange cSelectLogic;
					cSelectLogic.SetFrom(pcOpe->m_ptCaretPos_PHY_Before);
					cSelectLogic.SetTo(pcOpe->m_ptCaretPos_PHY_After);
					if( bFastMode ){
					}else{
						m_pCommanderView->GetSelectionInfo().m_sSelectBgn.SetFrom(ptCaretPos_Before);
						m_pCommanderView->GetSelectionInfo().m_sSelectBgn.SetTo(m_pCommanderView->GetSelectionInfo().m_sSelectBgn.GetFrom());
						m_pCommanderView->GetSelectionInfo().m_sSelect.SetFrom(ptCaretPos_Before);
						m_pCommanderView->GetSelectionInfo().m_sSelect.SetTo(ptCaretPos_After);
					}

					/* データ置換 削除&挿入にも使える */
					bDrawAll |= m_pCommanderView->ReplaceData_CEditView3(
						m_pCommanderView->GetSelectionInfo().m_sSelect,				// 削除範囲
						&pcInsertOpe->m_cOpeLineData,	// 削除されたデータのコピー(NULL可能)
						nullptr,
						bDraw,						// 再描画するか否か
						nullptr,
						pcInsertOpe->m_nOrgSeq,
						nullptr,
						bFastMode,
						&cSelectLogic
					);

					/* 選択範囲の変更 */
					m_pCommanderView->GetSelectionInfo().m_sSelectBgn.Clear(-1); //範囲選択(原点)
					m_pCommanderView->GetSelectionInfo().m_sSelect.Clear(-1);
				}
				break;
			case OPE_DELETE:
				{
					CDeleteOpe* pcDeleteOpe = static_cast<CDeleteOpe*>(pcOpe);

					//2007.10.17 kobake メモリリークしてました。修正。
					if( 0 < pcDeleteOpe->m_cOpeLineData.size() ){
						/* データ置換 削除&挿入にも使える */
						CLayoutRange sRange;
						sRange.Set(ptCaretPos_Before);
						CLogicRange cSelectLogic;
						cSelectLogic.Set(pcOpe->m_ptCaretPos_PHY_Before);
						bDrawAll |= m_pCommanderView->ReplaceData_CEditView3(
							sRange,
							nullptr,										/* 削除されたデータのコピー(NULL可能) */
							&pcDeleteOpe->m_cOpeLineData,
							bDraw,										/*再描画するか否か*/
							nullptr,
							0,
							&pcDeleteOpe->m_nOrgSeq,
							bFastMode,
							&cSelectLogic
						);
					}
					pcDeleteOpe->m_cOpeLineData.clear();
				}
				break;
			case OPE_REPLACE:
				{
					CReplaceOpe* pcReplaceOpe = static_cast<CReplaceOpe*>(pcOpe);

					CLayoutRange sRange;
					sRange.SetFrom(ptCaretPos_Before);
					sRange.SetTo(ptCaretPos_After);
					CLogicRange cSelectLogic;
					cSelectLogic.SetFrom(pcOpe->m_ptCaretPos_PHY_Before);
					cSelectLogic.SetTo(pcOpe->m_ptCaretPos_PHY_After);

					/* データ置換 削除&挿入にも使える */
					bDrawAll |= m_pCommanderView->ReplaceData_CEditView3(
						sRange,				// 削除範囲
						&pcReplaceOpe->m_pcmemDataIns,	// 削除されたデータのコピー(NULL可能)
						&pcReplaceOpe->m_pcmemDataDel,	// 挿入するデータ
						bDraw,						// 再描画するか否か
						nullptr,
						pcReplaceOpe->m_nOrgInsSeq,
						&pcReplaceOpe->m_nOrgDelSeq,
						bFastMode,
						&cSelectLogic
					);
					pcReplaceOpe->m_pcmemDataDel.clear();
				}
				break;
			case OPE_MOVECARET:
				/* カーソルを移動 */
				if( bFastMode ){
					GetCaret().MoveCursorFastMode( pcOpe->m_ptCaretPos_PHY_After );
				}else{
					GetCaret().MoveCursor( ptCaretPos_After, false );
				}
				break;
			default:
				break;
			}

			if( bFastMode ){
				if( i == 0 ){
					GetDocument()->m_cLayoutMgr._DoLayout(false);
					GetEditWindow()->ClearViewCaretPosInfo();
					if( GetDocument()->m_nTextWrapMethodCur == WRAP_NO_TEXT_WRAP ){
						// CLayoutMgr::_DoLayoutにて長さ算出済みなのでbCalLineLen=FALSE指定
						GetDocument()->m_cLayoutMgr.CalculateTextWidth(FALSE);
					}
					GetDocument()->m_cLayoutMgr.LogicToLayout(
						pcOpe->m_ptCaretPos_PHY_Before,
						&ptCaretPos_Before
					);
					GetCaret().MoveCursor( ptCaretPos_Before, true );
					// 通常モードではReplaceData_CEditViewの中で設定される
					GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX();
				}else{
					GetCaret().MoveCursorFastMode( pcOpe->m_ptCaretPos_PHY_Before );
				}
			}else{
				GetDocument()->m_cLayoutMgr.LogicToLayout(
					pcOpe->m_ptCaretPos_PHY_Before,
					&ptCaretPos_Before
				);
				if( i == 0 ){
					/* カーソルを移動 */
					GetCaret().MoveCursor( ptCaretPos_Before, true );
				}else{
					/* カーソルを移動 */
					GetCaret().MoveCursor( ptCaretPos_Before, false );
				}
			}
			if( hwndProgress && (i % 100) == 0 ){
				int newPos = ::MulDiv(nOpeBlkNum - i, 100, nOpeBlkNum);
				if( newPos != nProgressPos ){
					nProgressPos = newPos;
					ApiWrap::Progress_SetPos( hwndProgress, newPos + 1 );
					ApiWrap::Progress_SetPos( hwndProgress, newPos );
				}
			}
		}
		m_pCommanderView->SetDrawSwitch(bDrawSwitchOld);	//	hor
		m_pCommanderView->AdjustScrollBars(); // 2007.07.22 ryoji
		if (!bDraw) {
			GetCaret().ShowEditCaret();
		}

		/* Undo後の変更フラグ */
		GetDocument()->m_cDocEditor.SetModified(bIsModified,true);	//	Jan. 22, 2002 genta
		RestoreMultiCursorUndoPositions(*m_pCommanderView, multiCursorPositions);

		m_pCommanderView->m_bDoing_UndoRedo = false;	/* アンドゥ・リドゥの実行中か */

		m_pCommanderView->SetBracketPairPos( true );	// 03/03/07 ai

		/* 再描画 */
		// ルーラー再描画の必要があるときは DispRuler() ではなく他の部分と同時に Call_OnPaint() で描画する	// 2010.08.20 ryoji
		// ・DispRuler() はルーラーとテキストの隙間（左側は行番号の幅に合わせた帯）を描画してくれない
		// ・行番号表示に必要な幅は OPE_INSERT/OPE_DELETE 処理内で更新されており変更があればルーラー再描画フラグに反映されている
		// ・水平スクロールもルーラー再描画フラグに反映されている
		const bool bRedrawRuler = m_pCommanderView->GetRuler().GetRedrawFlag();
		m_pCommanderView->Call_OnPaint( PAINT_LINENUMBER | PAINT_BODY | (bRedrawRuler? PAINT_RULER: 0), false );
		if( !bRedrawRuler ){
			// ルーラーのキャレットのみを再描画
			HDC hdc = m_pCommanderView->GetDC();
			m_pCommanderView->GetRuler().DispRuler( hdc );
			m_pCommanderView->ReleaseDC( hdc );
		}

		GetCaret().ShowCaretPosInfo();	// キャレットの行桁位置を表示する	// 2007.10.19 ryoji

		if( !GetEditWindow()->UpdateTextWrap() && bDrawAll ){	// 折り返し方法関連の更新	// 2008.06.10 ryoji
			GetEditWindow()->RedrawAllViews( m_pCommanderView );	//	他のペインの表示を更新
		}

		if(hwndProgress) ::ShowWindow( hwndProgress, SW_HIDE );
	}

	GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().x;	// 2007.10.11 ryoji 追加
	m_pCommanderView->m_bDoing_UndoRedo = false;	/* アンドゥ・リドゥの実行中か */

	return;
}

//	from CViewCommander_New.cpp
/* Redo やり直し */
void CViewCommander::Command_REDO( void )
{
	if( m_pCommanderView->GetSelectionInfo().IsMouseSelecting() ){	/* マウスによる範囲選択中 */
		ErrorBeep();
		return;
	}

	{
		COpeBlk* opeBlk = m_pCommanderView->m_cCommander.GetOpeBlk();
		if( opeBlk ){
			int nCount = opeBlk->GetRefCount();
			opeBlk->SetRefCount(1); // 強制的にリセットするため1を指定
			m_pCommanderView->SetUndoBuffer();
			if( m_pCommanderView->m_cCommander.GetOpeBlk() == nullptr && 0 < nCount ){
				m_pCommanderView->m_cCommander.SetOpeBlk(new COpeBlk());
				m_pCommanderView->m_cCommander.GetOpeBlk()->SetRefCount( nCount );
			}
		}
		// 注意：Opeを追加するとRedoはできなくなる
	}

	if( !GetDocument()->m_cDocEditor.IsEnableRedo() ){	/* Redo(やり直し)可能な状態か？ */
		return;
	}
	MY_RUNNINGTIMER( cRunningTimer, L"CViewCommander::Command_REDO()" );

	COpe*		pcOpe = nullptr;
	COpeBlk*	pcOpeBlk;
	int			nOpeBlkNum;
	int			i;
//	int			nNewLine;	/* 挿入された部分の次の位置の行 */
//	int			nNewPos;	/* 挿入された部分の次の位置のデータ位置 */
	bool		bIsModified;
	MultiCursorUndoPositions multiCursorPositions{};

	CLayoutPoint ptCaretPos_Before;
	CLayoutPoint ptCaretPos_To;
	CLayoutPoint ptCaretPos_After;

	/* 各種モードの取り消し */
	Command_CANCEL_MODE();
	// Keep stale pre-edit positions out of redraws; tagged OPE records rebuild them after replay.
	m_pCommanderView->ClearExtraCursors();

	m_pCommanderView->m_bDoing_UndoRedo = true;	/* アンドゥ・リドゥの実行中か */

	/* 現在のRedo対象の操作ブロックを返す */
	if( nullptr != ( pcOpeBlk = GetDocument()->m_cDocEditor.m_cOpeBuf.DoRedo( &bIsModified ) ) ){
		multiCursorPositions = CaptureMultiCursorUndoPositions(pcOpeBlk, true);
		nOpeBlkNum = pcOpeBlk->GetNum();
		bool bDraw = (nOpeBlkNum < 5) && m_pCommanderView->GetDrawSwitch();
		bool bDrawAll = false;
		const bool bDrawSwitchOld = m_pCommanderView->SetDrawSwitch(bDraw);	// 2007.07.22 ryoji

		CWaitCursor cWaitCursor( m_pCommanderView->GetHwnd(), 1000 < nOpeBlkNum );
		HWND hwndProgress = nullptr;
		int nProgressPos = 0;
		if( cWaitCursor.IsEnable() ){
			hwndProgress = m_pCommanderView->StartProgress();
		}

		const bool bFastMode = (100 < nOpeBlkNum);
		for( i = 0; i < nOpeBlkNum; ++i ){
			pcOpe = pcOpeBlk->GetOpe( i );
			if( bFastMode ){
				if( i == 0 ){
					GetDocument()->m_cLayoutMgr.LogicToLayout(
						pcOpe->m_ptCaretPos_PHY_Before,
						&ptCaretPos_Before
					);
					GetCaret().MoveCursor( ptCaretPos_Before, true );
				}else{
					GetCaret().MoveCursorFastMode( pcOpe->m_ptCaretPos_PHY_Before );
				}
			}else{
				GetDocument()->m_cLayoutMgr.LogicToLayout(
					pcOpe->m_ptCaretPos_PHY_Before,
					&ptCaretPos_Before
				);
				if( i == 0 ){
					GetCaret().MoveCursor( ptCaretPos_Before, true );
				}else{
					GetCaret().MoveCursor( ptCaretPos_Before, false );
				}
			}
			switch( pcOpe->GetCode() ){
			case OPE_INSERT:
				{
					CInsertOpe* pcInsertOpe = static_cast<CInsertOpe*>(pcOpe);

					//2007.10.17 kobake メモリリークしてました。修正。
					if( 0 < pcInsertOpe->m_cOpeLineData.size() ){
						/* データ置換 削除&挿入にも使える */
						CLayoutRange sRange;
						sRange.Set(ptCaretPos_Before);
						CLogicRange cSelectLogic;
						cSelectLogic.Set(pcOpe->m_ptCaretPos_PHY_Before);
						bDrawAll |= m_pCommanderView->ReplaceData_CEditView3(
							sRange,
							nullptr,										/* 削除されたデータのコピー(NULL可能) */
							&pcInsertOpe->m_cOpeLineData,				/* 挿入するデータ */
							bDraw,										/*再描画するか否か*/
							nullptr,
							0,
							&pcInsertOpe->m_nOrgSeq,
							bFastMode,
							&cSelectLogic
						);
					}
					pcInsertOpe->m_cOpeLineData.clear();
				}
				break;
			case OPE_DELETE:
				{
					CDeleteOpe* pcDeleteOpe = static_cast<CDeleteOpe*>(pcOpe);

					if( bFastMode ){
					}else{
						GetDocument()->m_cLayoutMgr.LogicToLayout(
							pcDeleteOpe->m_ptCaretPos_PHY_To,
							&ptCaretPos_To
						);
					}
					CLogicRange cSelectLogic;
					cSelectLogic.SetFrom(pcOpe->m_ptCaretPos_PHY_Before);
					cSelectLogic.SetTo(pcDeleteOpe->m_ptCaretPos_PHY_To);

					/* データ置換 削除&挿入にも使える */
					bDrawAll |= m_pCommanderView->ReplaceData_CEditView3(
						CLayoutRange(ptCaretPos_Before,ptCaretPos_To),
						&pcDeleteOpe->m_cOpeLineData,	/* 削除されたデータのコピー(NULL可能) */
						nullptr,
						bDraw,
						nullptr,
						pcDeleteOpe->m_nOrgSeq,
						nullptr,
						bFastMode,
						&cSelectLogic
					);
				}
				break;
			case OPE_REPLACE:
				{
					CReplaceOpe* pcReplaceOpe = static_cast<CReplaceOpe*>(pcOpe);

					if( bFastMode ){
					}else{
						GetDocument()->m_cLayoutMgr.LogicToLayout(
							pcReplaceOpe->m_ptCaretPos_PHY_To,
							&ptCaretPos_To
						);
					}
					CLogicRange cSelectLogic;
					cSelectLogic.SetFrom(pcOpe->m_ptCaretPos_PHY_Before);
					cSelectLogic.SetTo(pcReplaceOpe->m_ptCaretPos_PHY_To);

					/* データ置換 削除&挿入にも使える */
					bDrawAll |= m_pCommanderView->ReplaceData_CEditView3(
						CLayoutRange(ptCaretPos_Before,ptCaretPos_To),
						&pcReplaceOpe->m_pcmemDataDel,	// 削除されたデータのコピー(NULL可能)
						&pcReplaceOpe->m_pcmemDataIns,	// 挿入するデータ
						bDraw,
						nullptr,
						pcReplaceOpe->m_nOrgDelSeq,
						&pcReplaceOpe->m_nOrgInsSeq,
						bFastMode,
						&cSelectLogic
					);
					pcReplaceOpe->m_pcmemDataIns.clear();
				}
				break;
			case OPE_MOVECARET:
				break;
			default:
				break;
			}
			if( bFastMode ){
				if( i == nOpeBlkNum - 1	){
					GetDocument()->m_cLayoutMgr._DoLayout(false);
					GetEditWindow()->ClearViewCaretPosInfo();
					if( GetDocument()->m_nTextWrapMethodCur == WRAP_NO_TEXT_WRAP ){
						// CLayoutMgr::_DoLayoutにて長さ算出済みなのでbCalLineLen=FALSE指定
						GetDocument()->m_cLayoutMgr.CalculateTextWidth(FALSE);
					}
					GetDocument()->m_cLayoutMgr.LogicToLayout(
						pcOpe->m_ptCaretPos_PHY_After, &ptCaretPos_After );
					GetCaret().MoveCursor( ptCaretPos_After, true );
					// 通常モードではReplaceData_CEditViewの中で設定される
					GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX();
				}else{
					GetCaret().MoveCursorFastMode( pcOpe->m_ptCaretPos_PHY_After );
				}
			}else{
				GetDocument()->m_cLayoutMgr.LogicToLayout(
					pcOpe->m_ptCaretPos_PHY_After, &ptCaretPos_After );
				if( i == nOpeBlkNum - 1	){
					GetCaret().MoveCursor( ptCaretPos_After, true );
				}else{
					GetCaret().MoveCursor( ptCaretPos_After, false );
				}
			}
			if( hwndProgress && (i % 100) == 0 ){
				int newPos = ::MulDiv(i + 1, 100, nOpeBlkNum);
				if( newPos != nProgressPos ){
					nProgressPos = newPos;
					ApiWrap::Progress_SetPos( hwndProgress, newPos + 1 );
					ApiWrap::Progress_SetPos( hwndProgress, newPos );
				}
			}
		}
		m_pCommanderView->SetDrawSwitch(bDrawSwitchOld); // 2007.07.22 ryoji
		m_pCommanderView->AdjustScrollBars(); // 2007.07.22 ryoji
		if (!bDraw) {
			GetCaret().ShowEditCaret();
		}

		/* Redo後の変更フラグ */
		GetDocument()->m_cDocEditor.SetModified(bIsModified,true);	//	Jan. 22, 2002 genta
		RestoreMultiCursorUndoPositions(*m_pCommanderView, multiCursorPositions);

		m_pCommanderView->m_bDoing_UndoRedo = false;	/* アンドゥ・リドゥの実行中か */

		m_pCommanderView->SetBracketPairPos( true );	// 03/03/07 ai

		/* 再描画 */
		// ルーラー再描画の必要があるときは DispRuler() ではなく他の部分と同時に Call_OnPaint() で描画する	// 2010.08.20 ryoji
		// ・DispRuler() はルーラーとテキストの隙間（左側は行番号の幅に合わせた帯）を描画してくれない
		// ・行番号表示に必要な幅は OPE_INSERT/OPE_DELETE 処理内で更新されており変更があればルーラー再描画フラグに反映されている
		// ・水平スクロールもルーラー再描画フラグに反映されている
		const bool bRedrawRuler = m_pCommanderView->GetRuler().GetRedrawFlag();
		m_pCommanderView->Call_OnPaint( PAINT_LINENUMBER | PAINT_BODY | (bRedrawRuler? PAINT_RULER: 0), false );
		if( !bRedrawRuler ){
			// ルーラーのキャレットのみを再描画
			HDC hdc = m_pCommanderView->GetDC();
			m_pCommanderView->GetRuler().DispRuler( hdc );
			m_pCommanderView->ReleaseDC( hdc );
		}

		GetCaret().ShowCaretPosInfo();	// キャレットの行桁位置を表示する	// 2007.10.19 ryoji

		if( !GetEditWindow()->UpdateTextWrap() && bDrawAll ){	// 折り返し方法関連の更新	// 2008.06.10 ryoji
			GetEditWindow()->RedrawAllViews( m_pCommanderView );	//	他のペインの表示を更新
		}

		if(hwndProgress) ::ShowWindow( hwndProgress, SW_HIDE );
	}

	GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().x;	// 2007.10.11 ryoji 追加
	m_pCommanderView->m_bDoing_UndoRedo = false;	/* アンドゥ・リドゥの実行中か */

	return;
}

//カーソル位置または選択エリアを削除
void CViewCommander::Command_DELETE( void )
{
	if( m_pCommanderView->GetSelectionInfo().IsMouseSelecting() ){		/* マウスによる範囲選択中 */
		ErrorBeep();
		return;
	}

	if( !m_pCommanderView->GetSelectionInfo().IsTextSelected() ){	/* テキストが選択されているか */
		// 2008.08.03 nasukoji	選択範囲なしでDELETEを実行した場合、カーソル位置まで半角スペースを挿入した後改行を削除して次行と連結する
		if( GetDocument()->m_cLayoutMgr.GetLineCount() > GetCaret().GetCaretLayoutPos().GetY2() ){
			const CLayout* pcLayout = GetDocument()->m_cLayoutMgr.SearchLineByLayoutY( GetCaret().GetCaretLayoutPos().GetY2() );
			if( pcLayout ){
				CLayoutInt nLineLen;
				CLogicInt nIndex;
				nIndex = m_pCommanderView->LineColumnToIndex2( pcLayout, GetCaret().GetCaretLayoutPos().GetX2(), &nLineLen );
				if( nLineLen != 0 ){	// 折り返しや改行コードより右の場合には nLineLen に行全体の表示桁数が入る
					if( pcLayout->GetLayoutEol().IsValid() ){	// 行終端は改行コードか?
						Command_INSTEXT( true, L"", CLogicInt(0), FALSE );	// カーソル位置まで半角スペース挿入
					}else{	// 行終端が折り返し
						// 折り返し行末ではスペース挿入後、次の文字を削除する	// 2009.02.19 ryoji

						// フリーカーソル時の折り返し越え位置での削除はどうするのが妥当かよくわからないが
						// 非フリーカーソル時（ちょうどカーソルが折り返し位置にある）には次の行の先頭文字を削除したい

						if( nLineLen < GetCaret().GetCaretLayoutPos().GetX2() ){	// 折り返し行末とカーソルの間に隙間がある
							Command_INSTEXT( true, L"", CLogicInt(0), FALSE );	// カーソル位置まで半角スペース挿入
							pcLayout = GetDocument()->m_cLayoutMgr.SearchLineByLayoutY( GetCaret().GetCaretLayoutPos().GetY2() );
							nIndex = m_pCommanderView->LineColumnToIndex2( pcLayout, GetCaret().GetCaretLayoutPos().GetX2(), &nLineLen );
						}
						if( nLineLen != 0 ){	// （スペース挿入後も）折り返し行末なら次文字を削除するために次行の先頭に移動する必要がある
							if( pcLayout->GetNextLayout() != nullptr ){	// 最終行末ではない
								CLayoutPoint ptLay;
								CLogicPoint ptLog(pcLayout->GetLogicOffset() + nIndex, pcLayout->GetLogicLineNo());
								GetDocument()->m_cLayoutMgr.LogicToLayout( ptLog, &ptLay );
								GetCaret().MoveCursor( ptLay, true );
								GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();
							}
						}
					}
				}
			}
		}
	}
	m_pCommanderView->DeleteData( true );
	return;
}

//カーソル前を削除
void CViewCommander::Command_DELETE_BACK( void )
{
	if( m_pCommanderView->GetSelectionInfo().IsMouseSelecting() ){	/* マウスによる範囲選択中 */
		ErrorBeep();
		return;
	}

	//	May 29, 2004 genta 実際に削除された文字がないときはフラグをたてないように
	//GetDocument()->m_cDocEditor.SetModified(true,true);	//	Jan. 22, 2002 genta
	if( m_pCommanderView->GetSelectionInfo().IsTextSelected() ){				/* テキストが選択されているか */
		m_pCommanderView->DeleteData( true );
	}
	else{
		CLayoutPoint	ptLayoutPos_Old = GetCaret().GetCaretLayoutPos();
		CLogicPoint		ptLogicPos_Old = GetCaret().GetCaretLogicPos();
		BOOL	bBool = Command_LEFT( false, false );
		if( bBool ){
			const CLayout* pcLayout = GetDocument()->m_cLayoutMgr.SearchLineByLayoutY( GetCaret().GetCaretLayoutPos().GetY2() );
			if( pcLayout ){
				CLayoutInt nLineLen;
				CLogicInt nIdx = m_pCommanderView->LineColumnToIndex2( pcLayout, GetCaret().GetCaretLayoutPos().GetX2(), &nLineLen );
				if( nLineLen == 0 ){	// 折り返しや改行コードより右の場合には nLineLen に行全体の表示桁数が入る
					// 右からの移動では折り返し末尾文字は削除するが改行は削除しない
					// 下から（下の行の行頭から）の移動では改行も削除する
					if( nIdx < pcLayout->GetLengthWithoutEOL() || GetCaret().GetCaretLayoutPos().GetY2() < ptLayoutPos_Old.GetY2() ){
						if( !m_pCommanderView->m_bDoing_UndoRedo ){	/* アンドゥ・リドゥの実行中か */
							/* 操作の追加 */
							GetOpeBlk()->AppendOpe(
								new CMoveCaretOpe(
									ptLogicPos_Old,
									GetCaret().GetCaretLogicPos()
								)
							);
							GetDocument()->m_cDocEditor.m_nOpeBlkRedawCount++;
						}
						m_pCommanderView->DeleteData( true );
					}
				}
			}
		}
	}
	m_pCommanderView->PostprocessCommand_hokan();	//	Jan. 10, 2005 genta 関数化
}

namespace {
struct SMultiCursorState {
	CLogicPoint caret;
	CLogicPoint anchor;
	CLogicPoint initialCaret;
	CLogicPoint initialAnchor;
	bool hasSelection = false;
	bool initialHasSelection = false;
	bool primary = false;
	size_t extraIndex = 0;
};

bool GetMultiCursorNextCharacter(const CEditDoc& document, const CLogicPoint& point, CLogicPoint* next)
{
	const CDocLine* line = document.m_cDocLineMgr.GetLine(point.y);
	if (!line || point.x < CLogicInt(0)) return false;
	const CLogicInt length = line->GetLengthWithoutEOL();
	if (point.x < length) {
		const int characterLength = CNativeW::GetSizeOfChar(line->GetPtr(), length, point.x);
		if (characterLength <= 0) return false;
		*next = CLogicPoint(CLogicInt(static_cast<int>(point.x) + characterLength), point.y);
		return true;
	}
	if (point.x == length && line->GetEol().IsValid() && point.y + CLogicInt(1) < document.m_cDocLineMgr.GetLineCount()) {
		*next = CLogicPoint(CLogicInt(0), point.y + CLogicInt(1));
		return true;
	}
	return false;
}

bool GetMultiCursorPreviousCharacter(const CEditDoc& document, const CLogicPoint& point, CLogicPoint* previous)
{
	if (point.x > CLogicInt(0)) {
		const CDocLine* line = document.m_cDocLineMgr.GetLine(point.y);
		if (!line) return false;
		const CLogicInt length = line->GetLengthWithoutEOL();
		CLogicInt index(0);
		CLogicInt prior(0);
		while (index < point.x && index < length) {
			prior = index;
			const int characterLength = CNativeW::GetSizeOfChar(line->GetPtr(), length, index);
			if (characterLength <= 0) return false;
			index += CLogicInt(characterLength);
		}
		if (index != point.x) return false;
		*previous = CLogicPoint(prior, point.y);
		return true;
	}
	if (point.y > CLogicInt(0)) {
		const CDocLine* priorLine = document.m_cDocLineMgr.GetLine(point.y - CLogicInt(1));
		if (!priorLine || !priorLine->GetEol().IsValid()) return false;
		*previous = CLogicPoint(priorLine->GetLengthWithoutEOL(), point.y - CLogicInt(1));
		return true;
	}
	return false;
}

void SetMultiCursorSelection(CEditView& view, const CLogicPoint& caret, const CLogicPoint& anchor, bool selected)
{
	CLayoutPoint caretLayout;
	view.GetDocument()->m_cLayoutMgr.LogicToLayout(caret, &caretLayout, CLayoutInt(0), false);
	view.GetCaret().MoveCursor(caretLayout, false);
	view.GetSelectionInfo().DisableSelectArea(false);
	if (!selected) {
		view.GetSelectionInfo().m_sSelectBgn.Set(caretLayout);
		return;
	}
	CLayoutPoint anchorLayout;
	view.GetDocument()->m_cLayoutMgr.LogicToLayout(anchor, &anchorLayout, CLayoutInt(0), false);
	CLayoutPoint from = anchorLayout;
	CLayoutPoint to = caretLayout;
	if (PointCompare(to, from) < 0) std::swap(from, to);
	view.GetSelectionInfo().m_sSelectBgn.Set(anchorLayout);
	view.GetSelectionInfo().m_sSelect = CLayoutRange(from, to);
}

struct SMultiCursorWordRange {
	CLogicPoint start;
	CLogicPoint end;
	std::wstring text;
};

bool GetMultiCursorWordAt(CEditView& view, const CLogicPoint& point, SMultiCursorWordRange* result)
{
	if (!result) return false;
	auto& layoutMgr = view.GetDocument()->m_cLayoutMgr;
	CLayoutPoint layoutPoint;
	layoutMgr.LogicToLayout(point, &layoutPoint, CLayoutInt(0), false);
	const CLayout* layout = layoutMgr.SearchLineByLayoutY(layoutPoint.GetY2());
	if (!layout) return false;
	CLayoutRange wordLayout;
	CNativeW word;
	const CLogicInt localIndex = view.LineColumnToIndex(layout, layoutPoint.GetX2());
	if (!layoutMgr.WhereCurrentWord(layoutPoint.GetY2(), localIndex, &wordLayout, &word, nullptr, true)
		|| word.GetStringLength() <= CLogicInt(0)) return false;
	layoutMgr.LayoutToLogic(wordLayout.GetFrom(), &result->start);
	layoutMgr.LayoutToLogic(wordLayout.GetTo(), &result->end);
	if (PointCompare(result->end, result->start) < 0) std::swap(result->start, result->end);
	result->text.assign(word.GetStringPtr(), static_cast<size_t>(word.GetStringLength()));
	return result->start != result->end;
}

bool GetMultiCursorRangeText(CEditDoc& document, CLogicPoint start, CLogicPoint end, std::wstring* text)
{
	if (!text) return false;
	if (PointCompare(end, start) < 0) std::swap(start, end);
	text->clear();
	for (CLogicInt lineNo = start.y; lineNo <= end.y; ++lineNo) {
		const CDocLine* line = document.m_cDocLineMgr.GetLine(lineNo);
		if (!line) return false;
		const CLogicInt length = line->GetLengthWithoutEOL();
		const CLogicInt from = lineNo == start.y ? std::clamp(start.x, CLogicInt(0), length) : CLogicInt(0);
		const CLogicInt to = lineNo == end.y ? std::clamp(end.x, CLogicInt(0), length) : length;
		if (to < from) return false;
		text->append(line->GetPtr() + static_cast<int>(from), static_cast<size_t>(to - from));
		if (lineNo < end.y) {
			const CEol& eol = line->GetEol();
			if (eol.IsValid()) text->append(eol.GetValue2(), static_cast<size_t>(eol.GetLen()));
			else text->append(document.m_cDocEditor.GetNewLineCode().GetValue2(),
				static_cast<size_t>(document.m_cDocEditor.GetNewLineCode().GetLen()));
		}
	}
	return true;
}

bool MultiCursorRangeAlreadySelected(CEditView& view, const CLogicPoint& start, const CLogicPoint& end)
{
	if (view.GetSelectionInfo().IsTextSelected()) {
		CLogicPoint anchor;
		view.GetDocument()->m_cLayoutMgr.LayoutToLogic(view.GetSelectionInfo().m_sSelectBgn.GetFrom(), &anchor);
		CLogicPoint caret = view.GetCaret().GetCaretLogicPos();
		if (PointCompare(caret, anchor) < 0) std::swap(caret, anchor);
		if (caret == end && anchor == start) return true;
	}
	for (const auto& extra : view.m_vExtraCursors) {
		CLayoutPoint extraCaretLayout;
		CLayoutPoint extraAnchorLayout;
		if (!view.ResolveExtraCursor(extra, &extraCaretLayout)
			|| !view.ResolveExtraCursorAnchor(extra, &extraAnchorLayout)) continue;
		CLogicPoint extraCaret;
		CLogicPoint extraAnchor;
		view.GetDocument()->m_cLayoutMgr.LayoutToLogic(extraCaretLayout, &extraCaret);
		view.GetDocument()->m_cLayoutMgr.LayoutToLogic(extraAnchorLayout, &extraAnchor);
		if (PointCompare(extraCaret, extraAnchor) < 0) std::swap(extraCaret, extraAnchor);
		if (extraAnchor == start && extraCaret == end) return true;
	}
	return false;
}

bool AddMultiCursorWordSelection(CEditView& view, const CLogicPoint& start, const CLogicPoint& end)
{
	if (!enhancementtoolbar::IsEnabled(enhancementtoolbar::Group::MultiCursor)
		|| view.m_vExtraCursors.size() >= CEditView::GetMaxExtraCursors()
		|| MultiCursorRangeAlreadySelected(view, start, end)) return false;
	CEditView::SExtraCursor extra;
	extra.caret = end;
	extra.bHasSelection = true;
	extra.anchor = start;
	view.m_vExtraCursors.push_back(extra);
	return true;
}

template <class Visitor>
void ForEachMultiCursorWordOccurrence(CEditView& view, std::wstring_view word, bool matchWholeWord, Visitor visitor)
{
	if (word.empty()) return;
	const auto lineCount = view.GetDocument()->m_cDocLineMgr.GetLineCount();
	for (CLogicInt lineNo(0); lineNo < lineCount; ++lineNo) {
		if (view.GetDocument()->m_cLayoutMgr.IsCodeFoldHiddenLine(lineNo)) continue;
		const CDocLine* line = view.GetDocument()->m_cDocLineMgr.GetLine(lineNo);
		if (!line) continue;
		const std::wstring_view text(line->GetPtr(), static_cast<size_t>(line->GetLengthWithoutEOL()));
		size_t offset = 0;
		while (offset <= text.size()) {
			const size_t found = text.find(word, offset);
			if (found == std::wstring_view::npos) break;
			const CLogicPoint rawStart(CLogicInt(static_cast<int>(found)), lineNo);
			const CLogicPoint rawEnd(CLogicInt(static_cast<int>(found + word.size())), lineNo);
			SMultiCursorWordRange candidate;
			CLogicPoint occurrenceStart = rawStart;
			CLogicPoint occurrenceEnd = rawEnd;
			bool exactOccurrence = true;
			if (matchWholeWord) {
				exactOccurrence = GetMultiCursorWordAt(view, rawStart, &candidate)
					&& candidate.start == rawStart
					&& candidate.end.y == lineNo
					&& static_cast<int>(candidate.end.x) - static_cast<int>(candidate.start.x) == static_cast<int>(word.size())
					&& candidate.text == word;
				if (exactOccurrence) {
					occurrenceStart = candidate.start;
					occurrenceEnd = candidate.end;
				}
			}
			if (exactOccurrence) {
				if (!visitor(occurrenceStart, occurrenceEnd)) return;
				offset = found + word.size();
			} else {
				offset = found + 1;
			}
		}
	}
}

bool AddExtraCursorAt(CEditView& view, const CLayoutPoint& point)
{
	if (!enhancementtoolbar::IsEnabled(enhancementtoolbar::Group::MultiCursor)) return false;
	CEditDoc* document = view.GetDocument();
	if (!document) return false;
	CLogicPoint logical;
	document->m_cLayoutMgr.LayoutToLogic(point, &logical);
	CEditView::SExtraCursor candidate;
	candidate.caret = logical;
	CLayoutPoint normalizedLayout;
	if (!view.ResolveExtraCursor(candidate, &normalizedLayout)) return false;
	document->m_cLayoutMgr.LayoutToLogic(normalizedLayout, &logical);
	if (logical == view.GetCaret().GetCaretLogicPos()) return false;
	for (const auto& extra : view.m_vExtraCursors) {
		CLayoutPoint existingLayout;
		CLogicPoint existing;
		if (view.ResolveExtraCursor(extra, &existingLayout)) {
			document->m_cLayoutMgr.LayoutToLogic(existingLayout, &existing);
			if (existing == logical) return false;
		}
	}
	if (view.m_vExtraCursors.size() >= CEditView::GetMaxExtraCursors()) return false;
	candidate.caret = logical;
	view.m_vExtraCursors.push_back(candidate);
	return true;
}

bool RemoveUnresolvableExtraCursors(CEditView& view)
{
	auto& cursors = view.m_vExtraCursors;
	const size_t originalSize = cursors.size();
	cursors.erase(std::remove_if(cursors.begin(), cursors.end(), [&](const CEditView::SExtraCursor& extra) {
		CLayoutPoint caret;
		CLayoutPoint anchor;
		return !view.ResolveExtraCursor(extra, &caret)
			|| (extra.bHasSelection && !view.ResolveExtraCursorAnchor(extra, &anchor));
	}), cursors.end());
	return cursors.size() != originalSize;
}
}

void CViewCommander::Command_AddCursorUp(void)
{
	if (!enhancementtoolbar::IsEnabled(enhancementtoolbar::Group::MultiCursor)) return;
	if (m_pCommanderView->GetSelectionInfo().IsBoxSelecting()) m_pCommanderView->GetSelectionInfo().DisableSelectArea(true);
	if (m_pCommanderView->m_vExtraCursors.size() >= CEditView::GetMaxExtraCursors()) { ErrorBeep(); return; }
	CLayoutPoint top = GetCaret().GetCaretLayoutPos();
	for (const auto& extra : m_pCommanderView->m_vExtraCursors) {
		CLayoutPoint point;
		if (m_pCommanderView->ResolveExtraCursor(extra, &point) && point.GetY2() < top.GetY2()) top = point;
	}
	const int targetLine = static_cast<int>(top.GetY2()) - 1;
	if (targetLine < 0) return;
	CLayoutPoint target;
	target.Set(top.GetX2(), CLayoutInt(targetLine));
	if (AddExtraCursorAt(*m_pCommanderView, target)) m_pCommanderView->Redraw();
}

void CViewCommander::Command_AddCursorDown(void)
{
	if (!enhancementtoolbar::IsEnabled(enhancementtoolbar::Group::MultiCursor)) return;
	if (m_pCommanderView->GetSelectionInfo().IsBoxSelecting()) m_pCommanderView->GetSelectionInfo().DisableSelectArea(true);
	if (m_pCommanderView->m_vExtraCursors.size() >= CEditView::GetMaxExtraCursors()) { ErrorBeep(); return; }
	CLayoutPoint bottom = GetCaret().GetCaretLayoutPos();
	for (const auto& extra : m_pCommanderView->m_vExtraCursors) {
		CLayoutPoint point;
		if (m_pCommanderView->ResolveExtraCursor(extra, &point) && point.GetY2() > bottom.GetY2()) bottom = point;
	}
	const int targetLine = static_cast<int>(bottom.GetY2()) + 1;
	if (targetLine >= static_cast<int>(GetDocument()->m_cLayoutMgr.GetLineCount())) return;
	CLayoutPoint target;
	target.Set(bottom.GetX2(), CLayoutInt(targetLine));
	if (AddExtraCursorAt(*m_pCommanderView, target)) m_pCommanderView->Redraw();
}

void CViewCommander::Command_SelectNextOccurrence(void)
{
	if (!enhancementtoolbar::IsEnabled(enhancementtoolbar::Group::MultiCursor)) return;
	auto& view = *m_pCommanderView;
	auto& selection = view.GetSelectionInfo();
	if (selection.IsBoxSelecting() || selection.IsMouseSelecting()) { ErrorBeep(); return; }
	const bool selectedTextMode = selection.IsTextSelected();
	bool matchWholeWord = !selectedTextMode;
	SMultiCursorWordRange seed;
	if (selectedTextMode) {
		CNativeW selected;
		if (!view.GetSelectedDataSimple(selected) || selected.GetStringLength() <= CLogicInt(0)) { ErrorBeep(); return; }
		selection.GetEditView()->GetDocument()->m_cLayoutMgr.LayoutToLogic(selection.m_sSelectBgn.GetFrom(), &seed.start);
		seed.end = GetCaret().GetCaretLogicPos();
		if (PointCompare(seed.end, seed.start) < 0) std::swap(seed.start, seed.end);
		seed.text.assign(selected.GetStringPtr(), static_cast<size_t>(selected.GetStringLength()));
		SMultiCursorWordRange wordAtSelection;
		matchWholeWord = GetMultiCursorWordAt(view, seed.start, &wordAtSelection)
			&& wordAtSelection.start == seed.start && wordAtSelection.end == seed.end
			&& wordAtSelection.text == seed.text;
	} else if (!GetMultiCursorWordAt(view, GetCaret().GetCaretLogicPos(), &seed)) {
		ErrorBeep(); return;
	}
	SetMultiCursorSelection(view, seed.end, seed.start, true);
	GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();

	bool reusable = true;
	for (const auto& extra : view.m_vExtraCursors) {
		CLayoutPoint extraCaret;
		CLayoutPoint extraAnchor;
		CLogicPoint caret;
		CLogicPoint anchor;
		if (!extra.bHasSelection || !view.ResolveExtraCursor(extra, &extraCaret)
			|| !view.ResolveExtraCursorAnchor(extra, &extraAnchor)) { reusable = false; break; }
		GetDocument()->m_cLayoutMgr.LayoutToLogic(extraCaret, &caret);
		GetDocument()->m_cLayoutMgr.LayoutToLogic(extraAnchor, &anchor);
		if (PointCompare(caret, anchor) < 0) std::swap(caret, anchor);
		std::wstring extraText;
		if (caret.y != seed.end.y || anchor.y != seed.start.y
			|| !GetMultiCursorRangeText(*GetDocument(), anchor, caret, &extraText)
			|| extraText != seed.text) { reusable = false; break; }
	}
	if (!reusable) view.m_vExtraCursors.clear();
		if (view.m_vExtraCursors.size() >= CEditView::GetMaxExtraCursors()) { ErrorBeep(); return; }

	CLogicPoint lastSelectedEnd = seed.end;
	for (const auto& extra : view.m_vExtraCursors) {
		CLayoutPoint extraCaret;
		if (view.ResolveExtraCursor(extra, &extraCaret)) {
			CLogicPoint caret;
			GetDocument()->m_cLayoutMgr.LayoutToLogic(extraCaret, &caret);
			if (PointCompare(lastSelectedEnd, caret) < 0) lastSelectedEnd = caret;
		}
	}
	CLogicPoint wrappedCandidateStart;
	CLogicPoint wrappedCandidateEnd;
	bool haveWrappedCandidate = false;
	bool foundAfter = false;
	ForEachMultiCursorWordOccurrence(view, seed.text, matchWholeWord, [&](const CLogicPoint& start, const CLogicPoint& end) {
		if (MultiCursorRangeAlreadySelected(view, start, end)) return true;
		if (PointCompare(start, lastSelectedEnd) > 0) {
			wrappedCandidateStart = start;
			wrappedCandidateEnd = end;
			foundAfter = true;
			return false;
		}
		if (!haveWrappedCandidate) {
			wrappedCandidateStart = start;
			wrappedCandidateEnd = end;
			haveWrappedCandidate = true;
		}
		return true;
	});
	if (!foundAfter && !haveWrappedCandidate) { ErrorBeep(); return; }
	const auto nextStart = wrappedCandidateStart;
	const auto nextEnd = wrappedCandidateEnd;
	if (!AddMultiCursorWordSelection(view, nextStart, nextEnd)) { ErrorBeep(); return; }
	view.Redraw();
}

void CViewCommander::Command_SelectAllOccurrences(void)
{
	if (!enhancementtoolbar::IsEnabled(enhancementtoolbar::Group::MultiCursor)) return;
	auto& view = *m_pCommanderView;
	auto& selection = view.GetSelectionInfo();
	if (selection.IsBoxSelecting() || selection.IsMouseSelecting()) { ErrorBeep(); return; }
	const bool selectedTextMode = selection.IsTextSelected();
	bool matchWholeWord = !selectedTextMode;
	SMultiCursorWordRange seed;
	if (selectedTextMode) {
		CNativeW selected;
		if (!view.GetSelectedDataSimple(selected) || selected.GetStringLength() <= CLogicInt(0)) { ErrorBeep(); return; }
		GetDocument()->m_cLayoutMgr.LayoutToLogic(selection.m_sSelectBgn.GetFrom(), &seed.start);
		seed.end = GetCaret().GetCaretLogicPos();
		if (PointCompare(seed.end, seed.start) < 0) std::swap(seed.start, seed.end);
		seed.text.assign(selected.GetStringPtr(), static_cast<size_t>(selected.GetStringLength()));
		SMultiCursorWordRange wordAtSelection;
		matchWholeWord = GetMultiCursorWordAt(view, seed.start, &wordAtSelection)
			&& wordAtSelection.start == seed.start && wordAtSelection.end == seed.end
			&& wordAtSelection.text == seed.text;
	} else if (!GetMultiCursorWordAt(view, GetCaret().GetCaretLogicPos(), &seed)) {
		ErrorBeep(); return;
	}
	view.m_vExtraCursors.clear();
	SetMultiCursorSelection(view, seed.end, seed.start, true);
	GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();
	bool limitReached = false;
	ForEachMultiCursorWordOccurrence(view, seed.text, matchWholeWord, [&](const CLogicPoint& start, const CLogicPoint& end) {
		if (start == seed.start && end == seed.end) return true;
		if (view.m_vExtraCursors.size() >= CEditView::GetMaxExtraCursors()) {
			limitReached = true;
			return false;
		}
		AddMultiCursorWordSelection(view, start, end);
		return true;
	});
	if (view.m_vExtraCursors.empty()) ErrorBeep();
	if (limitReached) ErrorBeep();
	view.Redraw();
}

void CViewCommander::Command_RemoveLastCursor(void)
{
	if (m_pCommanderView->m_vExtraCursors.empty()) return;
	m_pCommanderView->m_vExtraCursors.pop_back();
	m_pCommanderView->Redraw();
}

void CViewCommander::Command_ClearExtraCursors(void)
{
	m_pCommanderView->ClearExtraCursors();
}

void CViewCommander::ApplyMovementToAllCursors(const std::function<void()>& command)
{
	auto& view = *m_pCommanderView;
	const bool removedInvalidCursors = RemoveUnresolvableExtraCursors(view);
	if (view.m_vExtraCursors.empty()) {
		command();
		if (removedInvalidCursors) view.Redraw();
		return;
	}
	auto& layoutMgr = GetDocument()->m_cLayoutMgr;
	std::vector<SMultiCursorState> states;
	states.reserve(view.m_vExtraCursors.size() + 1);
	SMultiCursorState primary;
	primary.caret = GetCaret().GetCaretLogicPos();
	primary.anchor = primary.caret;
	primary.primary = true;
	primary.hasSelection = view.GetSelectionInfo().IsTextSelected();
	if (primary.hasSelection) layoutMgr.LayoutToLogic(view.GetSelectionInfo().m_sSelectBgn.GetFrom(), &primary.anchor);
	states.push_back(primary);
	for (size_t i = 0; i < view.m_vExtraCursors.size(); ++i) {
		const auto& extra = view.m_vExtraCursors[i];
		CLayoutPoint caretLayout;
		if (!view.ResolveExtraCursor(extra, &caretLayout)) continue;
		SMultiCursorState state;
		layoutMgr.LayoutToLogic(caretLayout, &state.caret);
		state.anchor = state.caret;
		state.extraIndex = i;
		CLayoutPoint anchorLayout;
		if (view.ResolveExtraCursorAnchor(extra, &anchorLayout)) {
			layoutMgr.LayoutToLogic(anchorLayout, &state.anchor);
			state.hasSelection = true;
		}
		states.push_back(state);
	}
	for (auto& state : states) {
		SetMultiCursorSelection(view, state.caret, state.anchor, state.hasSelection);
		GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();
		command();
		state.caret = GetCaret().GetCaretLogicPos();
		state.anchor = state.caret;
		state.hasSelection = view.GetSelectionInfo().IsTextSelected();
		if (state.hasSelection) layoutMgr.LayoutToLogic(view.GetSelectionInfo().m_sSelectBgn.GetFrom(), &state.anchor);
	}
	const auto primaryState = std::find_if(states.begin(), states.end(), [](const SMultiCursorState& state) { return state.primary; });
	if (primaryState == states.end()) return;
	SetMultiCursorSelection(view, primaryState->caret, primaryState->anchor, primaryState->hasSelection);
	GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();
	for (const auto& state : states) {
		if (state.primary) continue;
		auto& extra = view.m_vExtraCursors[state.extraIndex];
		extra.caret = state.caret;
		extra.bHasSelection = state.hasSelection;
		if (state.hasSelection) extra.anchor = state.anchor;
	}
	std::vector<CLayoutPoint> occupied{ GetCaret().GetCaretLayoutPos() };
	for (auto it = view.m_vExtraCursors.begin(); it != view.m_vExtraCursors.end();) {
		CLayoutPoint point;
		const bool resolved = view.ResolveExtraCursor(*it, &point);
		const bool duplicate = resolved && std::find(occupied.begin(), occupied.end(), point) != occupied.end();
		if (!resolved || duplicate) it = view.m_vExtraCursors.erase(it);
		else { occupied.push_back(point); ++it; }
	}
	view.Redraw();
}

void CViewCommander::ApplyToAllCursors(const std::function<void()>& command, EMultiCursorEditKind editKind)
{
	auto& view = *m_pCommanderView;
	if (!enhancementtoolbar::IsEnabled(enhancementtoolbar::Group::MultiCursor)) {
		view.ClearExtraCursors();
		command();
		return;
	}
	RemoveUnresolvableExtraCursors(view);
	auto& layoutMgr = GetDocument()->m_cLayoutMgr;
	std::vector<SMultiCursorState> states;
	states.reserve(view.m_vExtraCursors.size() + 1);

	SMultiCursorState primary;
	primary.caret = GetCaret().GetCaretLogicPos();
	primary.initialCaret = primary.caret;
	primary.anchor = primary.caret;
	primary.primary = true;
	primary.hasSelection = view.GetSelectionInfo().IsTextSelected();
	if (primary.hasSelection) layoutMgr.LayoutToLogic(view.GetSelectionInfo().m_sSelectBgn.GetFrom(), &primary.anchor);
	primary.initialAnchor = primary.anchor;
	primary.initialHasSelection = primary.hasSelection;
	states.push_back(primary);

	for (size_t i = 0; i < view.m_vExtraCursors.size(); ++i) {
		CLayoutPoint caretLayout;
		if (!view.ResolveExtraCursor(view.m_vExtraCursors[i], &caretLayout)) continue;
		SMultiCursorState state;
		layoutMgr.LayoutToLogic(caretLayout, &state.caret);
		state.initialCaret = state.caret;
		state.anchor = state.caret;
		state.extraIndex = i;
		CLayoutPoint anchorLayout;
		if (view.ResolveExtraCursorAnchor(view.m_vExtraCursors[i], &anchorLayout)) {
			layoutMgr.LayoutToLogic(anchorLayout, &state.anchor);
			state.hasSelection = true;
		}
		state.initialAnchor = state.anchor;
		state.initialHasSelection = state.hasSelection;
		states.push_back(state);
	}
	if (editKind == EMultiCursorEditKind::Overwrite
		&& GetDllShareData().m_Common.m_sEdit.m_bOverWriteFixMode
		&& std::any_of(states.begin(), states.end(), [](const SMultiCursorState& state) { return !state.hasSelection; })) {
		// Fixed-width overwrite may replace several characters and insert padding; reject the
		// mixed positional edit instead of risking stale offsets for the remaining cursors.
		ErrorBeep();
		return;
	}

	std::sort(states.begin(), states.end(), [](const SMultiCursorState& a, const SMultiCursorState& b) {
		const CLogicPoint aStart = PointCompare(a.caret, a.anchor) < 0 ? a.caret : a.anchor;
		const CLogicPoint bStart = PointCompare(b.caret, b.anchor) < 0 ? b.caret : b.anchor;
		const int cmp = PointCompare(aStart, bStart);
		if (cmp != 0) return cmp > 0;
		return PointCompare(a.caret, b.caret) > 0;
	});

	// Refuse intersecting selection ranges rather than applying an ambiguous edit twice.
	for (size_t i = 0; i < states.size(); ++i) {
		const CLogicPoint iStart = PointCompare(states[i].caret, states[i].anchor) < 0 ? states[i].caret : states[i].anchor;
		const CLogicPoint iEnd = PointCompare(states[i].caret, states[i].anchor) < 0 ? states[i].anchor : states[i].caret;
		for (size_t j = i + 1; j < states.size(); ++j) {
			const CLogicPoint jStart = PointCompare(states[j].caret, states[j].anchor) < 0 ? states[j].caret : states[j].anchor;
			const CLogicPoint jEnd = PointCompare(states[j].caret, states[j].anchor) < 0 ? states[j].anchor : states[j].caret;
			const bool iEmpty = iStart == iEnd;
			const bool jEmpty = jStart == jEnd;
			const bool overlaps = iEmpty && jEmpty
				? iStart == jStart
				: iEmpty ? PointCompare(jStart, iStart) <= 0 && PointCompare(iStart, jEnd) < 0
				: jEmpty ? PointCompare(iStart, jStart) <= 0 && PointCompare(jStart, iEnd) < 0
				: PointCompare(iStart, jEnd) < 0 && PointCompare(jStart, iEnd) < 0;
			if (overlaps) { ErrorBeep(); return; }
		}
	}

	COpeBlk* opeBlkBeforeMultiCursor = view.m_cCommander.GetOpeBlk();
	const int opeCountBeforeMultiCursor = opeBlkBeforeMultiCursor ? opeBlkBeforeMultiCursor->GetNum() : 0;

	for (size_t i = 0; i < states.size(); ++i) {
		auto& state = states[i];
		if (editKind == EMultiCursorEditKind::SelectedOnly && !state.hasSelection) continue;
		CLogicPoint oldStart = PointCompare(state.caret, state.anchor) < 0 ? state.caret : state.anchor;
		CLogicPoint oldEnd = PointCompare(state.caret, state.anchor) < 0 ? state.anchor : state.caret;
		if (!state.hasSelection && (editKind == EMultiCursorEditKind::DeleteForward || editKind == EMultiCursorEditKind::Overwrite)) {
			CLogicPoint next;
			const CDocLine* line = GetDocument()->m_cDocLineMgr.GetLine(state.caret.y);
			const bool protectedEol = editKind == EMultiCursorEditKind::Overwrite
				&& line && state.caret.x == line->GetLengthWithoutEOL()
				&& line->GetEol().IsValid()
				&& GetDllShareData().m_Common.m_sEdit.m_bNotOverWriteCRLF;
			if (!protectedEol && GetMultiCursorNextCharacter(*GetDocument(), state.caret, &next)) oldEnd = next;
		}
		if (!state.hasSelection && editKind == EMultiCursorEditKind::DeleteBackward) {
			CLogicPoint previous;
			if (GetMultiCursorPreviousCharacter(*GetDocument(), state.caret, &previous)) oldStart = previous;
		}
		SetMultiCursorSelection(view, state.caret, state.anchor, state.hasSelection);
		const int previousOpeSlot = view.m_nMultiCursorOpeSlot;
		view.m_nMultiCursorOpeSlot = state.primary ? 0 : static_cast<int>(state.extraIndex + 1);
		command();
		view.m_nMultiCursorOpeSlot = previousOpeSlot;
		const CLogicPoint newEnd = GetCaret().GetCaretLogicPos();

		// Edits are executed from the document end towards the start. Only already processed
		// (higher) cursors need their logical positions transformed by this edit.
		for (size_t processed = 0; processed < i; ++processed) {
			states[processed].caret = multicursor::TransformPointAfterReplace(states[processed].caret, oldStart, oldEnd, newEnd);
			states[processed].anchor = multicursor::TransformPointAfterReplace(states[processed].anchor, oldStart, oldEnd, newEnd);
		}
		state.caret = newEnd;
		state.anchor = newEnd;
		state.hasSelection = false;
		if (view.GetSelectionInfo().IsTextSelected()) {
			layoutMgr.LayoutToLogic(view.GetSelectionInfo().m_sSelectBgn.GetFrom(), &state.anchor);
			state.hasSelection = true;
		}
	}

	COpeBlk* multiCursorOpeBlk = view.m_cCommander.GetOpeBlk();
	if (multiCursorOpeBlk
		&& multiCursorOpeBlk->GetNum() > opeCountBeforeMultiCursor) {
		// A tagged no-op caret record preserves slots that had no text operation (for example,
		// an unselected cursor during a selected-only edit) and records each slot's final point.
		for (const auto& state : states) {
			auto* cursorOpe = new CMultiCursorStateOpe(state.initialCaret, state.caret,
				state.initialAnchor, state.anchor, state.initialHasSelection, state.hasSelection);
			cursorOpe->m_nMultiCursorSlot = state.primary ? 0 : static_cast<int>(state.extraIndex + 1);
			multiCursorOpeBlk->AppendOpe(cursorOpe);
		}
	}

	const auto primaryState = *std::find_if(states.begin(), states.end(), [](const SMultiCursorState& state) { return state.primary; });
	SetMultiCursorSelection(view, primaryState.caret, primaryState.anchor, primaryState.hasSelection);
	GetCaret().m_nCaretPosX_Prev = GetCaret().GetCaretLayoutPos().GetX2();

	for (const auto& state : states) {
		if (state.primary) continue;
		auto& extra = view.m_vExtraCursors[state.extraIndex];
		extra.caret = state.caret;
		extra.bHasSelection = state.hasSelection;
		if (state.hasSelection) extra.anchor = state.anchor;
	}

	// Remove cursors that collapsed onto an existing caret after an edit.
	std::vector<CLayoutPoint> occupied{ GetCaret().GetCaretLayoutPos() };
	for (auto it = view.m_vExtraCursors.begin(); it != view.m_vExtraCursors.end();) {
		CLayoutPoint point;
		const bool resolved = view.ResolveExtraCursor(*it, &point);
		const bool duplicate = resolved && std::find(occupied.begin(), occupied.end(), point) != occupied.end();
		if (!resolved || duplicate) {
			it = view.m_vExtraCursors.erase(it);
		} else {
			occupied.push_back(point);
			++it;
		}
	}
	view.Redraw();
}

/* 	上書き用の一文字削除	2009.04.11 ryoji */
void CViewCommander::DelCharForOverwrite( const wchar_t* pszInput, int nLen )
{
	bool bEol = false;
	BOOL bDelete = TRUE;
	const CLayout* pcLayout = GetDocument()->m_cLayoutMgr.SearchLineByLayoutY( GetCaret().GetCaretLayoutPos().GetY2() );
	int nDelLen = 0;
	CKetaXInt nKetaDiff = CKetaXInt(0);
	CKetaXInt nKetaAfterIns = CKetaXInt(0);
	if( nullptr != pcLayout ){
		/* 指定された桁に対応する行のデータ内の位置を調べる */
		CLogicInt nIdxTo = m_pCommanderView->LineColumnToIndex( pcLayout, GetCaret().GetCaretLayoutPos().GetX2() );
		if( nIdxTo >= pcLayout->GetLengthWithoutEOL() ){
			bEol = true;	// 現在位置は改行または折り返し以後
			if( pcLayout->GetLayoutEol().IsValid() ){
				if( GetDllShareData().m_Common.m_sEdit.m_bNotOverWriteCRLF ){	/* 改行は上書きしない */
					/* 現在位置が改行ならば削除しない */
					bDelete = FALSE;
				}
			}
		}else{
			// 文字幅に合わせてスペースを詰める
			if( GetDllShareData().m_Common.m_sEdit.m_bOverWriteFixMode ){
				const CStringRef line = pcLayout->GetDocLineRef()->GetStringRefWithEOL();
				CLogicInt nPos = GetCaret().GetCaretLogicPos().GetX();
				if( line.At(nPos) != WCODE::TAB ){
					CKetaXInt nKetaBefore = CNativeW::GetKetaOfChar(line, nPos);
					CKetaXInt nKetaAfter = CNativeW::GetKetaOfChar(pszInput, nLen, 0);
					nKetaDiff = nKetaBefore - nKetaAfter;
					nPos += CNativeW::GetSizeOfChar(line.GetPtr(), line.GetLength(), nPos);
					nDelLen = 1;
					if( nKetaDiff < 0 && nPos < line.GetLength() ){
						wchar_t c = line[nPos];
						if( c != WCODE::TAB && !WCODE::IsLineDelimiter(c,
								GetDllShareData().m_Common.m_sEdit.m_bEnableExtEol) ){
							nDelLen = 2;
							CKetaXInt nKetaBefore2 = CNativeW::GetKetaOfChar(line, nPos);
							nKetaAfterIns = nKetaBefore + nKetaBefore2 - nKetaAfter;
						}
					}
				}
			}
		}
	}
	if( bDelete ){
		/* 上書きモードなので、現在位置の文字を１文字消去 */
		CLayoutPoint posBefore;
		if( bEol ){
			Command_DELETE();	//行数減では再描画が必要＆行末以後の削除を処理統一
			posBefore = GetCaret().GetCaretLayoutPos();
		}else{
			// 1文字削除
			m_pCommanderView->DeleteData( false );
			posBefore = GetCaret().GetCaretLayoutPos();
			for(int i = 1; i < nDelLen; i++){
				m_pCommanderView->DeleteData( false );
			}
		}
		CNativeW tmp;
		for(CKetaXInt i = CKetaXInt(0); i < nKetaDiff; i++){
			tmp.AppendString(L" ");
		}
		for(CKetaXInt i = CKetaXInt(0); i < nKetaAfterIns; i++){
			tmp.AppendString(L" ");
		}
		if( 0 < tmp.GetStringLength() ){
			Command_INSTEXT( false, tmp.GetStringPtr(), tmp.GetStringLength(), false, false);
			GetCaret().MoveCursor(posBefore, false);
		}
	}
}
