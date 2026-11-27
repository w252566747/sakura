#!/usr/bin/env python3
"""Compile and exercise the production rainbow bracket state helpers."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def check_ui_and_persistence_wiring() -> None:
    toolbar = (ROOT / "sakura_core/window/CMainToolBar.cpp").read_text(encoding="utf-8-sig")
    settings = (ROOT / "sakura_core/env/CShareData_IO.cpp").read_text(encoding="utf-8-sig")
    view = (ROOT / "sakura_core/view/CEditView.cpp").read_text(encoding="utf-8-sig")
    bracket_view = (ROOT / "sakura_core/view/CEditView_Paint_Bracket.cpp").read_text(encoding="utf-8-sig")
    colors = (ROOT / "sakura_core/view/colors/CColorStrategy.cpp").read_text(encoding="utf-8-sig")
    layout = (ROOT / "sakura_core/doc/layout/CLayoutMgr_DoLayout.cpp").read_text(encoding="utf-8-sig")
    assert "RAINBOW_CURRENT_LEVEL_MENU" in toolbar and "現在の階層を強調" in toolbar
    assert "SetRainbowCurrentLevelHighlightEnabled(!currentLevelEnabled)" in toolbar
    assert "L\"HighlightCurrentLevel\"" in settings
    assert "SetRainbowCurrentLevelHighlightEnabled(false)" in settings
    assert "IsRainbowCurrentLevelHighlightEnabled() ? L\"1\" : L\"0\"" in settings
    assert "SetCurrentRainbowBracketLevel(static_cast<int>(caretMark.level))" in bracket_view
    assert "ShouldHighlightRainbowBracketMark" in colors
    assert "g_bRainbowUnmatchedHighlightEnabled = false" in view
    assert "SetRainbowUnmatchedHighlightEnabled(false)" in settings
    assert "HighlightUnmatched" in settings
    assert "if( bCheckColorEnabled ){" in layout and "color.CheckColorMODE" in layout
    assert "const bool inCommentOrString =" in layout
    assert "if (!inCommentOrString && !IsEscapedCurlBrace(lineText" in layout
    assert "ContainsCurlTypeName(m_pTypeConfig->m_szTypeName)" in layout
    assert "IsInsideCurlTextExpressionString()" in layout
    assert "quote == L'\"' && ch == L'{'" in layout
    assert "ch == L'\"' || ch == L'\\''" in layout
    assert "curlsyntax::TryGetVerbatimTerminator" in layout
    assert "curlsyntax::TryGetCommentTerminator" in layout
    assert "curlVerbatimOpenerLength - 1" in layout
    assert "IsInsideCurlVerbatim()" in layout
    assert "} else if (ch == L'}' && !IsEscapedCurlBrace(lineText, position))" not in layout


def main() -> None:
    check_ui_and_persistence_wiring()
    with tempfile.TemporaryDirectory(prefix="sakura-rainbow-bracket-") as temp:
        source = Path(temp) / "main.cpp"
        executable = Path(temp) / "rainbow-bracket-test"
        source.write_text(r'''#include "doc/layout/BracketNestingState.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <vector>

int main() {
  CBracketNestingState levels;
  const unsigned expectedOpenLevels[] = {0, 1, 2, 3, 4, 5, 0};
  for (unsigned expected : expectedOpenLevels) {
    unsigned color = 99;
    bool bad = false;
    assert(levels.Consume(L'(', &color, &bad) && !bad && color == expected);
  }
  const unsigned expectedCloseLevels[] = {0, 5, 4, 3, 2, 1, 0};
  for (unsigned expected : expectedCloseLevels) {
    unsigned color = 99;
    bool bad = false;
    assert(levels.Consume(L')', &color, &bad) && !bad && color == expected);
  }

  assert(IsEscapedCurlBrace(L"\\{", 1));
  assert(!IsEscapedCurlBrace(L"\\\\{", 2));
  assert(IsEscapedCurlBrace(L"\\\\\\}", 3));
  assert(!IsEscapedCurlBrace(L"(\\)", 2));
  assert(IsRainbowBracketCharacter(L'(') && IsRainbowBracketCharacter(L'}'));
  assert(!IsRainbowBracketCharacter(L'a'));

  std::wstring terminator;
  std::size_t verbatimLength = 0;
  std::size_t openerLength = 0;
  assert(curlsyntax::TryGetVerbatimTerminator(L"|\"body\"|", 0, terminator, verbatimLength, openerLength));
  assert(terminator == L"\"|" && verbatimLength == std::numeric_limits<std::size_t>::max());
  assert(openerLength == 2);
  assert(curlsyntax::TryGetVerbatimTerminator(L"|tag-x\"body\"|tag-x|", 0, terminator, verbatimLength, openerLength));
  assert(terminator == L"\"|tag-x|" && verbatimLength == std::numeric_limits<std::size_t>::max());
  assert(openerLength == 7);
  assert(curlsyntax::TryGetVerbatimTerminator(L"|tag?\"body\"|tag?|", 0, terminator, verbatimLength, openerLength));
  assert(terminator == L"\"|tag?|" && verbatimLength == std::numeric_limits<std::size_t>::max());
  assert(curlsyntax::TryGetVerbatimTerminator(L"|5\"abcde\"5|", 0, terminator, verbatimLength, openerLength));
  assert(terminator == L"\"5|" && verbatimLength == 5);
  assert(openerLength == 3);
  assert(!curlsyntax::TryGetVerbatimTerminator(L"|bad tag\"x\"bad tag|", 0, terminator, verbatimLength, openerLength));
  assert(!curlsyntax::TryGetVerbatimTerminator(L"|999999999999999999999999999999999999\"x\"999999999999999999999999999999999999|", 0, terminator, verbatimLength, openerLength));
  assert(curlsyntax::TryGetCommentTerminator(L"|# body #|", 0, terminator, verbatimLength, openerLength));
  assert(terminator == L"#|" && verbatimLength == std::numeric_limits<std::size_t>::max());
  assert(openerLength == 2);
  assert(curlsyntax::TryGetCommentTerminator(L"|tag?# body #tag?|", 0, terminator, verbatimLength, openerLength));
  assert(terminator == L"#tag?|" && verbatimLength == std::numeric_limits<std::size_t>::max());
  assert(curlsyntax::TryGetCommentTerminator(L"|4#body#4|", 0, terminator, verbatimLength, openerLength));
  assert(terminator == L"#4|" && verbatimLength == 4);

  CBracketNestingState expression;
  expression.BeginCurlTextExpression();
  unsigned level = 0;
  bool mismatched = false;
  assert(expression.Consume(L'{', &level, &mismatched) && !mismatched);
  expression.ToggleCurlTextExpressionString(L'"');
  expression.BeginCurlTextExpression();
  assert(expression.Consume(L'{', &level, &mismatched) && !mismatched);
  assert(expression.Consume(L'(', &level, &mismatched) && !mismatched);
  assert(expression.Consume(L')', &level, &mismatched) && !mismatched);
  assert(expression.Consume(L'}', &level, &mismatched) && !mismatched);
  expression.EndCurlTextExpressionIfBalanced();
  assert(expression.IsInsideCurlTextExpressionString());
  assert(expression.GetCurlTextExpressionQuote() == L'"');
  expression.ToggleCurlTextExpressionString(L'"');
  expression.ToggleCurlTextExpressionString(L'\'');
  assert(expression.GetCurlTextExpressionQuote() == L'\'');
  expression.ToggleCurlTextExpressionString(L'\'');
  assert(expression.Consume(L'}', &level, &mismatched) && !mismatched);
  expression.EndCurlTextExpressionIfBalanced();
  assert(!expression.IsInsideCurlTextExpression());

  CBracketNestingState mismatchedCloser;
  unsigned mismatchColor = 99;
  assert(mismatchedCloser.Consume(L'(', &level, &mismatched) && !mismatched);
  assert(mismatchedCloser.Consume(L']', &mismatchColor, &mismatched) && mismatched);
  assert(mismatchColor == 0); // A type-mismatched closer does not borrow the inner level color.

  CBracketNestingState raw;
  raw.BeginCurlVerbatim(L"\"tag|");
  assert(raw.IsInsideCurlVerbatim());
  for (wchar_t ch : std::wstring(L" first line { [ (\nsecond line ) ] }\"ta"))
    assert(raw.ConsumeCurlVerbatim(ch));
  assert(raw.IsInsideCurlVerbatim());
  assert(raw.ConsumeCurlVerbatim(L'g'));
  assert(raw.ConsumeCurlVerbatim(L'|'));
  assert(!raw.IsInsideCurlVerbatim());
  assert(raw == CBracketNestingState());
  CBracketNestingState rawLength;
  rawLength.BeginCurlVerbatim(L"\"12|");
  rawLength.ConsumeCurlVerbatim(L'{');
  CBracketNestingState rawChanged;
  rawChanged.BeginCurlVerbatim(L"\"13|");
  assert(rawLength != rawChanged);
  CBracketNestingState exactLength;
  exactLength.BeginCurlVerbatim(L"\"5|", 5);
  for (wchar_t ch : std::wstring(L"x\"5|y")) assert(exactLength.ConsumeCurlVerbatim(ch));
  assert(exactLength.IsInsideCurlVerbatim()); // An apparent terminator inside the declared body is raw text.
  for (wchar_t ch : std::wstring(L"\"5|")) assert(exactLength.ConsumeCurlVerbatim(ch));
  assert(!exactLength.IsInsideCurlVerbatim());
  CBracketNestingState overlappingTerminator;
  overlappingTerminator.BeginCurlVerbatim(L"abab");
  for (wchar_t ch : std::wstring(L"abacabab")) assert(overlappingTerminator.ConsumeCurlVerbatim(ch));
  assert(!overlappingTerminator.IsInsideCurlVerbatim());

  std::vector<SRainbowBracketMark> correct(6);
  const wchar_t chars[] = { L'(', L'[', L'{', L'}', L']', L')' };
  CBracketMatchResolver good;
  for (std::size_t i = 0; i < correct.size(); ++i) good.Consume(chars[i], &correct[i]);
  good.Finish();
  for (const auto& mark : correct) assert(!mark.unmatched);

  std::vector<SRainbowBracketMark> malformed(4);
  const wchar_t badChars[] = { L'(', L'[', L')', L']' };
  CBracketMatchResolver bad;
  for (std::size_t i = 0; i < malformed.size(); ++i) bad.Consume(badChars[i], &malformed[i]);
  bad.Finish();
  assert(malformed[0].unmatched && !malformed[1].unmatched);
  assert(malformed[2].unmatched && !malformed[3].unmatched);

  std::vector<SRainbowBracketMark> unclosed(2);
  CBracketMatchResolver eof;
  eof.Consume(L'(', &unclosed[0]);
  eof.Consume(L'{', &unclosed[1]);
  eof.Finish();
  assert(unclosed[0].unmatched && unclosed[1].unmatched);

  const SRainbowBracketMark outer{0, 0, false};
  const SRainbowBracketMark inner{1, 1, false};
  const SRainbowBracketMark unmatched{2, 2, true};
  assert(ShouldHighlightRainbowBracketMark(outer, 0, true, false));
  assert(!ShouldHighlightRainbowBracketMark(inner, 0, true, false));
  assert(ShouldHighlightRainbowBracketMark(inner, 1, true, false));
  assert(!ShouldHighlightRainbowBracketMark(inner, -1, true, false));
  // With unmatched emphasis disabled, malformed brackets retain their
  // nesting-depth color instead of borrowing the matched-pair highlight color.
  assert(!ShouldHighlightRainbowBracketMark(unmatched, 2, false, false));
  assert(ShouldHighlightRainbowBracketMark(unmatched, -1, false, true));
  std::cout << "rainbow bracket nesting, mismatch and EOF cases passed\n";
}
''', encoding="utf-8")
        subprocess.run([
            "clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=undefined", "-I", str(ROOT / "sakura_core"),
            str(source), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
