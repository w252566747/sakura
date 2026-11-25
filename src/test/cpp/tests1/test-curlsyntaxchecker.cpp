/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "curl/CurlSyntaxChecker.h"
#include "curl/CurlSyntaxCheckService.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace {
const curlsyntax::Diagnostic* FindCode(const std::vector<curlsyntax::Diagnostic>& diagnostics, const char* code)
{
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code) return &diagnostic;
	}
	return nullptr;
}
}

TEST(CurlSyntaxChecker, IgnoresBracketsInStringsAndComments)
{
	const auto diagnostics = curlsyntax::Check(
		L"{VBox value = \"{not code, }\"}\n"
		L"|| {comment, [ignored]}\n"
		L"{TextField value = |\"{also ignored, }\"|}\n"
		L"|# {comment, [ignored]} #|\n"
		L"|tag# {tagged comment, {ignored}} #tag|\n"
		L"|3#{} #3|\n"
		L"|txt\"{tagged verbatim, {ignored}}\"txt|\n"
		L"|21\"{ignored, (brackets)}\"21|");
	EXPECT_TRUE(diagnostics.empty());
}

TEST(CurlSyntaxChecker, DoesNotTreatTopLevelQuotedTextAsAStringLiteral)
{
	const auto diagnostics = curlsyntax::Check(L"A quoted \"text with {bold words}\" is top-level Curl text.");
	EXPECT_TRUE(diagnostics.empty());
}

TEST(CurlSyntaxChecker, ReportsUnclosedBlockComment)
{
	const auto diagnostics = curlsyntax::Check(L"{VBox |# {ignored}");
	EXPECT_NE(FindCode(diagnostics, "CURL010"), nullptr);
}

TEST(CurlSyntaxChecker, ReportsUnclosedCurlBracesAtOpeningPosition)
{
	const auto diagnostics = curlsyntax::Check(L"{VBox\n  {TextField");
	const auto* error = FindCode(diagnostics, "CURL004");
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(error->line, 1);
	EXPECT_EQ(error->column, 1);
	EXPECT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(), [](const auto& d) {
		return d.code == "CURL004";
	}), 2);
}

TEST(CurlSyntaxChecker, ReportsUnmatchedRoundAndSquareBrackets)
{
	const auto diagnostics = curlsyntax::Check(L"{call (value]} ");
	EXPECT_NE(FindCode(diagnostics, "CURL006"), nullptr);
	const auto square = curlsyntax::Check(L"{call [value}");
	EXPECT_NE(FindCode(square, "CURL007"), nullptr);
}

TEST(CurlSyntaxChecker, DiagnosticsAreSortedBySourcePosition)
{
	const auto diagnostics = curlsyntax::Check(L"{]\n{");
	ASSERT_GE(diagnostics.size(), 2u);
	EXPECT_TRUE(std::is_sorted(diagnostics.begin(), diagnostics.end(), [](const auto& left, const auto& right) {
		return left.offset < right.offset;
	}));
}

TEST(CurlSyntaxChecker, ValidatesCurlEscapesAndUnclosedStrings)
{
	const auto diagnostics = curlsyntax::Check(L"{let a = \"bad\\q\"}\n{let b = \"open}");
	EXPECT_NE(FindCode(diagnostics, "CURL011"), nullptr);
	EXPECT_NE(FindCode(diagnostics, "CURL008"), nullptr);
}

TEST(CurlSyntaxChecker, WarnsForLikelyMisspelledBuiltInNames)
{
	const auto diagnostics = curlsyntax::Check(L"{TextFeld value = \"x\"}\n{define-procc {example}} ");
	ASSERT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
		return diagnostic.code == "CURL012";
	}), 2);
	EXPECT_NE(FindCode(diagnostics, "CURL012"), nullptr);
}

TEST(CurlSyntaxChecker, DoesNotWarnForCustomIdentifiersAndProperties)
{
	const auto diagnostics = curlsyntax::Check(L"{TextField value = StringBuf2, custom-property = MyCustomPanel}");
	EXPECT_EQ(FindCode(diagnostics, "CURL012"), nullptr);
}

TEST(CurlSyntaxChecker, AcceptsCommaSeparatedIdentifierArguments)
{
	const auto diagnostics = curlsyntax::Check(L"{some-proc alpha, beta, gamma}");
	EXPECT_EQ(FindCode(diagnostics, "CURL003"), nullptr);
	EXPECT_EQ(FindCode(diagnostics, "CURL013"), nullptr);
}

TEST(CurlSyntaxChecker, ReportsRepeatedAndTrailingCommas)
{
	const auto leading = curlsyntax::Check(L"{some-proc , alpha}");
	EXPECT_NE(FindCode(leading, "CURL014"), nullptr);
	for (const wchar_t* source : {
		L"{some-proc alpha,, beta}",
		L"{some-proc alpha, beta,, gamma}"
	}) {
		const auto diagnostics = curlsyntax::Check(source);
		EXPECT_NE(FindCode(diagnostics, "CURL003"), nullptr) << source;
	}
	const auto trailing = curlsyntax::Check(L"{some-proc alpha, beta,}");
	EXPECT_NE(FindCode(trailing, "CURL013"), nullptr);
}

TEST(CurlSyntaxChecker, WarnsForDeletedCharacterInShortKeywordAndTypeName)
{
	const auto keyword = curlsyntax::Check(L"{whil condition}");
	const auto* keywordWarning = FindCode(keyword, "CURL012");
	ASSERT_NE(keywordWarning, nullptr);
	EXPECT_NE(keywordWarning->message.find(L"while"), std::wstring::npos);

	const auto type = curlsyntax::Check(L"{define-proc {f value:Strng}:void {return}}");
	const auto* typeWarning = FindCode(type, "CURL012");
	ASSERT_NE(typeWarning, nullptr);
	EXPECT_NE(typeWarning->message.find(L"String"), std::wstring::npos);
}

TEST(CurlSyntaxChecker, WarnsForMisspelledLanguageWordsOutsideExpressionHeads)
{
	const auto diagnostics = curlsyntax::Check(
		L"{define-class Example\n"
		L"  class-procc public {example-method}\n"
		L"  {define-proc {run}:int {retun 1}}\n"
		L"}");
	ASSERT_EQ(std::count_if(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
		return diagnostic.code == "CURL012";
	}), 2);
	EXPECT_NE(std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
		return diagnostic.message.find(L"class-proc") != std::wstring::npos;
	}), diagnostics.end());
	EXPECT_NE(std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
		return diagnostic.message.find(L"return") != std::wstring::npos;
	}), diagnostics.end());
}

TEST(CurlSyntaxChecker, WarnsForDeletedCharacterInFlowKeywords)
{
	for (const auto& sample : {
		std::pair{L"{cath value}", L"catch"},
		std::pair{L"{brek value}", L"break"},
		std::pair{L"{thrw value}", L"throw"},
		std::pair{L"{whil value}", L"while"}
	}) {
		const auto diagnostics = curlsyntax::Check(sample.first);
		const auto* warning = FindCode(diagnostics, "CURL012");
		ASSERT_NE(warning, nullptr);
		EXPECT_NE(warning->message.find(sample.second), std::wstring::npos);
	}
}

TEST(CurlSyntaxChecker, IgnoresOrdinaryParenthesesAndBracketsInTopLevelText)
{
	const auto diagnostics = curlsyntax::Check(L"Text (normal) [display] outside Curl code.");
	EXPECT_TRUE(diagnostics.empty());
}

TEST(CurlSyntaxChecker, RecognizesNewHeadAcrossLineBreak)
{
	const auto diagnostics = curlsyntax::Check(L"{new\nTextFeld value = 1}");
	const auto* warning = FindCode(diagnostics, "CURL012");
	ASSERT_NE(warning, nullptr);
	EXPECT_EQ(warning->line, 2);
	EXPECT_EQ(warning->column, 1);
}

TEST(CurlSyntaxChecker, DoesNotInventCommaErrorsForMacroBodies)
{
	const auto diagnostics = curlsyntax::Check(
		L"{define-syntax {my-syntax x} {\n"
		L"  {VBox\n"
		L"    {TextField value = x}\n"
		L"  }\n"
		L"}}");
	EXPECT_TRUE(diagnostics.empty());
}

TEST(CurlSyntaxChecker, ReportsRepeatedCommasOutsideStringsAndComments)
{
	const auto diagnostics = curlsyntax::Check(
		L"{VBox spacing=10pt, {TextField value=\"A,,B\"}, || , ignored\n"
		L"  {CommandButton label=\"検索\"},, {CommandButton label=\"終了\"}} ");
	const auto* error = FindCode(diagnostics, "CURL003");
	ASSERT_NE(error, nullptr);
	EXPECT_EQ(error->line, 2);
}

TEST(CurlSyntaxChecker, ReportsMissingCommaBetweenKnownLayoutChildren)
{
	for (const wchar_t* container : { L"VBox", L"TextFlowBox", L"Table" }) {
		std::wstring source = L"{";
		source += container;
		source += L"\n {TextField value=\"ABC\"}\n {CommandButton label=\"検索\"}}";
		const auto diagnostics = curlsyntax::Check(source);
		EXPECT_NE(FindCode(diagnostics, "CURL001"), nullptr) << container;
	}
}

TEST(CurlSyntaxChecker, HandlesLargeInputLinearly)
{
	std::wstring source;
	source.reserve(2 * 1024 * 1024);
	for (int i = 0; i < 100000; ++i) source += L"{TextField value = \"safe, text\"}\n";
	const auto diagnostics = curlsyntax::Check(source);
	EXPECT_TRUE(diagnostics.empty());
}

TEST(CurlSyntaxSettings, ErrorListHeightIsClamped)
{
	curlsyntax::SetErrorListHeight(20);
	EXPECT_EQ(curlsyntax::GetErrorListHeight(), 80);
	curlsyntax::SetErrorListHeight(900);
	EXPECT_EQ(curlsyntax::GetErrorListHeight(), 600);
	curlsyntax::SetErrorListHeight(150);
}

TEST(CurlSyntaxCheckService, ReplacesQueuedRequestAndPreservesDocumentVersion)
{
	std::mutex mutex;
	std::condition_variable ready;
	std::optional<curlsyntax::CheckResult> result;
	curlsyntax::CheckService service(std::chrono::milliseconds(30));
	auto complete = [&](curlsyntax::CheckResult value) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			result = std::move(value);
		}
		ready.notify_one();
	};
	service.Schedule(11, 1, L"{", complete);
	service.Schedule(27, 4, L"{}", complete);
	std::unique_lock<std::mutex> lock(mutex);
	ASSERT_TRUE(ready.wait_for(lock, std::chrono::seconds(2), [&] { return result.has_value(); }));
	EXPECT_EQ(result->documentId, 27u);
	EXPECT_EQ(result->version, 4u);
	EXPECT_FALSE(result->manual);
	EXPECT_TRUE(result->diagnostics.empty());
}


TEST(CurlSyntaxCheckService, ManualRequestRunsImmediatelyAndIsTagged)
{
	std::mutex mutex;
	std::condition_variable ready;
	std::optional<curlsyntax::CheckResult> result;
	curlsyntax::CheckService service(std::chrono::seconds(5));
	service.Schedule(42, 9, L"{}", [&](curlsyntax::CheckResult value) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			result = std::move(value);
		}
		ready.notify_one();
	}, true, true);
	std::unique_lock<std::mutex> lock(mutex);
	ASSERT_TRUE(ready.wait_for(lock, std::chrono::milliseconds(500), [&] { return result.has_value(); }));
	EXPECT_EQ(result->documentId, 42u);
	EXPECT_EQ(result->version, 9u);
	EXPECT_TRUE(result->manual);
}

TEST(CurlSyntaxCheckService, CancelRemovesPendingWork)
{
	std::atomic_bool completed = false;
	curlsyntax::CheckService service(std::chrono::milliseconds(100));
	service.Schedule(1, 1, L"{}", [&](curlsyntax::CheckResult) {
		completed.store(true, std::memory_order_relaxed);
	});
	service.Cancel();
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	EXPECT_FALSE(completed.load(std::memory_order_relaxed));
}
