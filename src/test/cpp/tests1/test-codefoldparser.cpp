#include "pch.h"
#include "parse/CodeFoldParser.h"

#include <string>
#include <string_view>
#include <vector>

namespace {
std::vector<codefold::Region> ParseLines(const std::vector<std::wstring>& lines, codefold::Language language)
{
	std::vector<std::wstring_view> views;
	views.reserve(lines.size());
	for (const auto& line : lines) views.emplace_back(line);
	return codefold::Parse(views, language);
}
}

TEST(CodeFoldParser, FindsNestedCurlBlocksAndIgnoresCommentsAndStrings)
{
	const std::vector<std::wstring> lines = {
		L"{define-proc public {hello} ->",
		L"    {if ready then",
		L"        {VBox",
		L"            \"a } brace\" || } comment",
		L"        }",
		L"    }",
		L"}"
	};
	const auto regions = ParseLines(lines, codefold::Language::Curl);
	ASSERT_EQ(regions.size(), 3u);
	EXPECT_EQ(regions[0].firstLine, 0u);
	EXPECT_EQ(regions[0].lastLine, 6u);
	EXPECT_EQ(regions[1].firstLine, 1u);
	EXPECT_EQ(regions[1].lastLine, 5u);
	EXPECT_EQ(regions[2].firstLine, 2u);
	EXPECT_EQ(regions[2].lastLine, 4u);
}

TEST(CodeFoldParser, SupportsCppJavaScriptAndJsonBraceSyntax)
{
	const std::vector<std::wstring> lines = {
		L"function f() {",
		L"  const text = `}`; // ignored }",
		L"  if (ok) {",
		L"    return { value: 1 };",
		L"  }",
		L"}"
	};
	const auto regions = ParseLines(lines, codefold::Language::CLike);
	ASSERT_EQ(regions.size(), 2u);
	EXPECT_EQ(regions.front().firstLine, 0u);
	EXPECT_EQ(regions.front().lastLine, 5u);
	EXPECT_EQ(regions.back().firstLine, 2u);
	EXPECT_EQ(regions.back().lastLine, 4u);
}

TEST(CodeFoldParser, KeepsMultilineAndLineContinuedTextOutOfBraceMatching)
{
	const std::vector<std::wstring> lines = {
		L"void f() {",
		L"  const char* text = \"continued \\",
		L"} string\";",
		L"  // continued comment \\",
		L"  } this brace is still in the comment",
		L"  if (ready) {",
		L"  }",
		L"}"
	};
	const auto regions = ParseLines(lines, codefold::Language::CLike);
	ASSERT_EQ(regions.size(), 2u);
	EXPECT_EQ(regions[0].firstLine, 0u);
	EXPECT_EQ(regions[0].lastLine, 7u);
	EXPECT_EQ(regions[1].firstLine, 5u);
	EXPECT_EQ(regions[1].lastLine, 6u);

	const std::vector<std::wstring> templateLines = {
		L"function f() {",
		L"  const text = `multi-line template",
		L"} template contents are not code",
		L"{ still template`;",
		L"  if (ready) {",
		L"  }",
		L"}"
	};
	const auto templateRegions = ParseLines(templateLines, codefold::Language::CLike);
	ASSERT_EQ(templateRegions.size(), 2u);
	EXPECT_EQ(templateRegions.front().lastLine, 6u);
	EXPECT_EQ(templateRegions.back().firstLine, 4u);
	EXPECT_EQ(templateRegions.back().lastLine, 5u);
}

TEST(CodeFoldParser, MatchesXmlTagsAndSkipsCommentsAndSelfClosingNodes)
{
	const std::vector<std::wstring> lines = {
		L"<root>",
		L"  <!-- <fake> -->",
		L"  <item id=\"a>b\">",
		L"    <leaf />",
		L"  </item>",
		L"</root>"
	};
	const auto regions = ParseLines(lines, codefold::Language::Xml);
	ASSERT_EQ(regions.size(), 2u);
	EXPECT_EQ(regions[0].kind, L"root");
	EXPECT_EQ(regions[0].firstLine, 0u);
	EXPECT_EQ(regions[0].lastLine, 5u);
	EXPECT_EQ(regions[1].kind, L"item");
	EXPECT_EQ(regions[1].firstLine, 2u);
	EXPECT_EQ(regions[1].lastLine, 4u);
}

TEST(CodeFoldParser, FindsNestedPlSqlBlocksAndIgnoresQuotedKeywords)
{
	const std::vector<std::wstring> lines = {
		L"BEGIN",
		L"  IF ready THEN",
		L"    dbms_output.put_line('END IF;');",
		L"  END IF;",
		L"END;"
	};
	const auto regions = ParseLines(lines, codefold::Language::Sql);
	ASSERT_EQ(regions.size(), 2u);
	EXPECT_EQ(regions[0].kind, L"BEGIN");
	EXPECT_EQ(regions[0].firstLine, 0u);
	EXPECT_EQ(regions[0].lastLine, 4u);
	EXPECT_EQ(regions[1].kind, L"IF");
	EXPECT_EQ(regions[1].firstLine, 1u);
	EXPECT_EQ(regions[1].lastLine, 3u);
}

TEST(CodeFoldParser, IgnoresSqlKeywordsInsideMultilineString)
{
	const std::vector<std::wstring> lines = {
		L"BEGIN",
		L"  INSERT INTO messages VALUES ('text starts",
		L"END IF; and continues');",
		L"  IF ready THEN",
		L"    NULL;",
		L"  END IF;",
		L"END;"
	};
	const auto regions = ParseLines(lines, codefold::Language::Sql);
	ASSERT_EQ(regions.size(), 2u);
	EXPECT_EQ(regions[0].kind, L"BEGIN");
	EXPECT_EQ(regions[0].firstLine, 0u);
	EXPECT_EQ(regions[0].lastLine, 6u);
	EXPECT_EQ(regions[1].kind, L"IF");
	EXPECT_EQ(regions[1].firstLine, 3u);
	EXPECT_EQ(regions[1].lastLine, 5u);
}

TEST(CodeFoldParser, LeavesUnmatchedStructuresVisible)
{
	const std::vector<std::wstring> curl = { L"{define-proc", L"  {if true}", L"" };
	const std::vector<std::wstring> xml = { L"<root>", L"  <child/>" };
	const std::vector<std::wstring> sql = { L"BEGIN", L"  NULL;" };
	EXPECT_TRUE(ParseLines(curl, codefold::Language::Curl).empty());
	EXPECT_TRUE(ParseLines(xml, codefold::Language::Xml).empty());
	EXPECT_TRUE(ParseLines(sql, codefold::Language::Sql).empty());
}
