/*
 Copyright (C) 2026 Sakura Editor Organization
 Custom enhancement regression fixture. SPDX-License-Identifier: Zlib

 Compiles the actual AppendHTMLColor and Command_COPY_COLOR_HTML bodies with
 small document/layout/clipboard doubles. This checks selection traversal and
 serialization, not the Windows clipboard API or Sakura's syntax lexer.
*/
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cwchar>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>
using CLogicInt = int;
using CLayoutInt = int;
using Int = int;
using WCHAR = wchar_t;
using COLORREF = uint32_t;
using DWORD = uint32_t;
unsigned GetRValue(COLORREF c) { return c & 255; }
unsigned GetGValue(COLORREF c) { return (c >> 8) & 255; }
unsigned GetBValue(COLORREF c) { return (c >> 16) & 255; }
template<size_t N, class... T> int _swprintf(wchar_t (&b)[N], const wchar_t* f, T... v) {
 return std::swprintf(b, N, f, v...);
}
struct CNativeW {
 std::wstring s;
 CNativeW() = default;
 CNativeW(const wchar_t* p, int n): s(p, n) {}
 void AppendString(const wchar_t* p) { s += p; }
 void AppendString(const wchar_t* p, int n) { assert(n >= 0); s.append(p, n); }
 void AppendNativeData(const CNativeW& b) { s += b.s; }
 void Replace(const wchar_t* a, const wchar_t* b) {
  size_t pos = 0;
  while ((pos = s.find(a, pos)) != s.npos) { s.replace(pos, wcslen(a), b); pos += wcslen(b); }
 }
 void AllocStringBuffer(size_t n) { s.reserve(n); }
 size_t capacity() const { return s.capacity(); }
 size_t GetStringLength() const { return s.size(); }
 const wchar_t* GetStringPtr() const { return s.c_str(); }
};
namespace WCODE {
 const wchar_t* CRLF = L"\r\n";
 bool IsLineDelimiter(wchar_t c, bool) { return c == L'\n' || c == L'\r'; }
}
struct Share { struct { struct { bool m_bAddCRLFWhenCopy = false; bool m_bEnableExtEol = false; } m_sEdit; } m_Common; } share;
Share& GetDllShareData() { return share; }
struct Point { int x = 0, y = 0; bool operator==(Point p) const { return x == p.x && y == p.y; } int GetY() const { return y; } };
struct CLogicRange {
 Point a, b;
 void Clear(int v) { a = {v,v}; b = a; }
 void SetFromY(int v) { a.y = v; } void SetFromX(int v) { a.x = v; }
 void SetToY(int v) { b.y = v; } void SetToX(int v) { b.x = v; }
 Point GetFrom() const { return a; } Point GetTo() const { return b; }
};
struct CLayoutRect { int left, right, top, bottom; };
void TwoPointToRect(CLayoutRect* r, Point a, Point b) {
 if (a.y > b.y || (a.y == b.y && a.x > b.x)) std::swap(a,b);
 *r = {a.x,b.x,a.y,b.y};
}
struct Eol { int n = 0; int GetLen() const { return n; } };
struct CDocLine {
 std::wstring s; CDocLine* next = nullptr;
 const wchar_t* GetPtr() const { return s.c_str(); }
 int GetLengthWithEOL() const { return static_cast<int>(s.size()); }
 const CDocLine* GetNextLine() const { return next; }
};
struct CLayout {
 CDocLine* doc; int line, offset, length; Eol eol; CLayout* next = nullptr; CLayout* prev = nullptr;
 const CLayout* GetNextLayout() const { return next; } const CLayout* GetPrevLayout() const { return prev; }
 int GetLogicOffset() const { return offset; } int GetLogicLineNo() const { return line; }
 int GetLengthWithoutEOL() const { return length; } Eol GetLayoutEol() const { return eol; }
 const CDocLine* GetDocLineRef() const { return doc; } const wchar_t* GetPtr() const { return doc->GetPtr()+offset; }
 int GetColorTypePrev() const { return 0; } int GetColorInfo() const { return 0; }
};
struct CLayoutMgr {
 std::vector<std::unique_ptr<CLayout>> rows; std::set<int> hidden;
 bool IsCodeFoldActive() const { return !hidden.empty(); }
 bool IsCodeFoldHiddenLine(int n) const { return hidden.count(n) != 0; }
 const CLayout* GetTopLayout() const { return rows.empty()?nullptr:rows.front().get(); }
 const wchar_t* GetLineStr(int y, int* length, const CLayout** out) const {
  *out = nullptr;
  for (const auto& row: rows) if (!IsCodeFoldHiddenLine(row->line) && y-- == 0) {
   *out = row.get(); *length = row->length; return row->GetPtr();
  }
  return nullptr;
 }
};
struct SColorAttr { COLORREF m_cTEXT, m_cBACK; };
struct SFontAttr { bool m_bBoldFont, m_bUnderLine; };
struct ColorInfo { SColorAttr m_sColorAttr; SFontAttr m_sFontAttr; };
struct STypeConfig { ColorInfo m_ColorInfoArr[2] = {{{0,0xffffff},{false,false}},{{0xff,0xffffff},{false,false}}}; bool m_bLineNumIsCRLF = true; };
constexpr int COLORIDX_TEXT = 0;
int ToColorInfoArrIndex(int i) { return i; }
struct DocType { STypeConfig type; const STypeConfig& GetDocumentAttribute() const { return type; } };
struct Document {
 CLayoutMgr m_cLayoutMgr; DocType m_cDocType;
 struct Lines { std::vector<CDocLine> lines; int GetLineCount() const { return static_cast<int>(lines.size()); } } m_cDocLineMgr;
};
struct Selection {
 Point from, to; bool box = false;
 bool IsTextSelected() const { return !(from == to); } bool IsBoxSelecting() const { return box; }
 Point GetFrom() const { return from; } Point GetTo() const { return to; }
};
struct View {
 Selection selection;
 const Selection& GetSelectionInfo() const { return selection; }
 int LineColumnToIndex(const CLayout* row, int col) const { return std::clamp(col, 0, row->length); }
};
struct CStringRef { const wchar_t* p; int length; CStringRef(const wchar_t* p_, int n):p(p_),length(n){} };
struct CColorStrategy {
 int color = 0;
 void InitStrategyStatus() {} void SetStrategyColorInfo(int) {}
 int GetStrategyColor() const { return color; }
};
struct CColorStrategyPool {
 CColorStrategy strategy;
 static CColorStrategyPool* getInstance() { static CColorStrategyPool p; return &p; }
 void SetCurrentView(View*) {} void NotifyOnStartScanLogic() {}
 CColorStrategy* GetStrategyByColor(int c) { strategy.color=c; return &strategy; }
};
std::wstring clipboard;
struct CClipboard {
 explicit CClipboard(int) {} explicit operator bool() const { return true; }
 void Empty() { clipboard.clear(); } void SetHtmlText(const CNativeW& b) { clipboard = b.s; }
 void SetText(const wchar_t*, size_t, bool, bool) {}
};
struct CViewCommander {
 Document document; View view; View* m_pCommanderView = &view;
 Document* GetDocument() { return &document; } Selection& GetSelect() { return view.selection; }
 CViewCommander* GetEditWindow() { return this; } int GetHwnd() { return 0; }
 void Command_COPY_COLOR_HTML(bool);
 CColorStrategy* GetColorStrategyHTML(const CStringRef& line, int i, CColorStrategyPool* pool, CColorStrategy**, CColorStrategy**, bool& changed) {
  assert(i >= 0 && i < line.length); changed = true;
  pool->strategy.color = line.p[i] == L'R' ? 1 : 0;
  return &pool->strategy;
 }
 void Init(std::vector<std::wstring> lines, int wrap = 0) {
  auto& docs = document.m_cDocLineMgr.lines; docs.resize(lines.size());
  for (size_t n=0; n<lines.size(); ++n) { docs[n].s=lines[n]; docs[n].next=n+1<lines.size()?&docs[n+1]:nullptr; }
  auto& rows = document.m_cLayoutMgr.rows;
  for (size_t n=0; n<docs.size(); ++n) {
   int full=static_cast<int>(docs[n].s.size()), eol=full && docs[n].s.back()==L'\n' ? (full>1 && docs[n].s[full-2]==L'\r'?2:1):0;
   int content=full-eol;
   for (int offset=0;;) {
    int len=wrap>0?std::min(wrap,content-offset):content;
    bool last=offset+len==content;
    auto row=std::make_unique<CLayout>(CLayout{&docs[n],static_cast<int>(n),offset,len,{last?eol:0}});
    if (!rows.empty()) { row->prev=rows.back().get(); rows.back()->next=row.get(); }
    rows.push_back(std::move(row)); offset+=len; if(last) break;
   }
  }
 }
 std::wstring Copy(Point a, Point b, bool box=false, bool numbers=false) {
  view.selection={a,b,box}; Command_COPY_COLOR_HTML(numbers); return clipboard;
 }
};
// PRODUCTION_BODIES
std::wstring Plain(const std::wstring& html) {
 std::wstring out; bool tag=false;
 for (wchar_t c:html) { if(c==L'<') tag=true; else if(c==L'>') tag=false; else if(!tag) out+=c; }
 return out;
}
void Expect(const std::wstring& actual, const std::wstring& expected) {
 if(actual!=expected) { std::wcerr << L"Expected ["<<expected<<L"] got ["<<actual<<L"]\n"; std::abort(); }
}
int main() {
 { CViewCommander c; c.Init({L"head\n",L"R<&\n",L"hidden\n",L"tail\n"}); c.document.m_cLayoutMgr.hidden={1,2};
   const auto html=c.Copy({1,0},{2,1}); Expect(Plain(html),L"ead\nR&lt;&amp;\nhidden\nta");
   assert(html.find(L"color:#ff0000")!=html.npos); assert(c.document.m_cLayoutMgr.hidden.size()==2);
   Expect(Plain(c.Copy({2,1},{1,0})),L"ead\nR&lt;&amp;\nhidden\nta");
   Expect(Plain(c.Copy({1,0},{3,1},true)),L"ea\r\nai\r\n");
   Expect(Plain(c.Copy({0,1},{2,1},false,true)),L"4:ta");
   Expect(Plain(c.Copy({0,0},{0,1},false,true)),L"1:head\n2:R&lt;&amp;\n3:hidden\n");
 }
 { CViewCommander c; c.Init({L"0123456789\n",L"Rabcdef\n",L"tail"},4); c.document.m_cLayoutMgr.hidden={1};
   Expect(Plain(c.Copy({1,1},{1,3})),L"56789\nRabcdef\nt");
   Expect(Plain(c.Copy({1,1},{2,1})),L"5");
   Expect(Plain(c.Copy({1,0},{2,1})),L"12345");
   share.m_Common.m_sEdit.m_bAddCRLFWhenCopy=true;
   Expect(Plain(c.Copy({1,1},{2,1},false,true)),L" :5\r\n");
   share.m_Common.m_sEdit.m_bAddCRLFWhenCopy=false;
 }
 { CViewCommander c; c.Init({L"a\r\n",L"R\r\n",L"z"}); c.document.m_cLayoutMgr.hidden={1};
   Expect(Plain(c.Copy({0,0},{0,2})),L"a\r\nR\r\nz");
 }
 { CViewCommander c; std::vector<std::wstring> lines(12,L"hidden\n"); lines.front()=L"head\n"; lines.back()=L"tail";
   c.Init(lines); for(int n=1;n<11;++n) c.document.m_cLayoutMgr.hidden.insert(n);
   c.document.m_cDocType.type.m_bLineNumIsCRLF=false;
   Expect(Plain(c.Copy({0,0},{2,1},false,true)),L" 1:head\n 2:hidden\n 3:hidden\n 4:hidden\n 5:hidden\n 6:hidden\n 7:hidden\n 8:hidden\n 9:hidden\n10:hidden\n11:hidden\n12:ta");
 }
 // Compare all visible endpoint pairs against the original text, with and
 // without folding and wrapping. This oracle does not use production traversal.
 size_t checked = 0;
 for (int wrap : {0, 3, 5}) for (bool folded : {false, true}) {
  CViewCommander c; c.Init({L"abcde\r\n", L"R中日文\n", L"inside\n", L"last012"}, wrap);
  auto& mgr = c.document.m_cLayoutMgr;
  if (folded) mgr.hidden={1,2};
  std::vector<const CLayout*> visible;
  for (const auto& row : mgr.rows) if (!mgr.IsCodeFoldHiddenLine(row->line)) visible.push_back(row.get());
  std::wstring original;
  std::vector<size_t> starts;
  for (const auto& line : c.document.m_cDocLineMgr.lines) { starts.push_back(original.size()); original+=line.s; }
  for (size_t a=0; a<visible.size(); ++a) for(size_t b=a; b<visible.size(); ++b) {
   for(int x=0; x<=visible[a]->length; ++x) for(int y=0; y<=visible[b]->length; ++y) {
    size_t from=starts[visible[a]->line]+visible[a]->offset+x;
    size_t to=starts[visible[b]->line]+visible[b]->offset+y;
    if (to<=from) continue;
    Expect(Plain(c.Copy({x,static_cast<int>(a)},{y,static_cast<int>(b)})), original.substr(from,to-from));
    ++checked;
   }
  }
  std::wstring after;
  for(const auto& line : c.document.m_cDocLineMgr.lines) after+=line.s;
  assert(original==after);
  assert(mgr.hidden.size()==(folded?2u:0u));
 }
 std::cout << checked << " original-text selection comparisons passed\n";
 std::cout << "HTML copy: folded/reverse/rectangular/wrapped/EOF/CRLF/line-number/color cases passed\n";
}
