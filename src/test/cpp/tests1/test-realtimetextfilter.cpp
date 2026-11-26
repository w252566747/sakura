#include "pch.h"
#include "parse/RealtimeTextFilter.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>

TEST(RealtimeTextFilter, AppliesAndOrCaseAndExcludeTerms)
{
	textfilter::Options options;
	options.includeTerms = { L"error", L"disk" };
	options.excludeTerms = { L"ignore" };
	textfilter::Matcher matcher(options);
	EXPECT_TRUE(matcher.Matches(L"DISK ERROR"));
	EXPECT_FALSE(matcher.Matches(L"disk only"));
	EXPECT_FALSE(matcher.Matches(L"disk error ignore"));

	options.logic = textfilter::Logic::Or;
	textfilter::Matcher orMatcher(options);
	EXPECT_TRUE(orMatcher.Matches(L"disk only"));
	EXPECT_FALSE(orMatcher.Matches(L"disk only ignore"));
}

TEST(RealtimeTextFilter, EmptyIncludeShowsAllLinesExceptExcludedTerms)
{
	textfilter::Options options;
	options.excludeTerms = { L"DEBUG" };
	textfilter::Matcher matcher(options);
	EXPECT_TRUE(matcher.Matches(L"INFO ready"));
	EXPECT_FALSE(matcher.Matches(L"DEBUG detail"));
}

TEST(RealtimeTextFilter, SupportsSeparateRegularExpressionsAndReportsInvalidPattern)
{
	textfilter::Options options;
	options.includeTerms = { L"^WARN", L"[0-9]+$" };
	options.logic = textfilter::Logic::And;
	options.regularExpression = true;
	textfilter::Matcher matcher(options);
	EXPECT_TRUE(matcher.Matches(L"WARN 42"));
	EXPECT_FALSE(matcher.Matches(L"WARN none"));
	EXPECT_FALSE(matcher.Matches(L"INFO 42"));

	options.includeTerms = { L"[" };
	EXPECT_THROW((textfilter::Matcher(options)), textfilter::FilterError);
}

TEST(RealtimeTextFilter, MergesOverlappingContextAndClampsToDocument)
{
	const auto ranges = textfilter::BuildVisibleRanges({ 1, 4, 9 }, 10, 1);
	ASSERT_EQ(ranges.size(), 2u);
	EXPECT_EQ(ranges[0].first, 0u);
	EXPECT_EQ(ranges[0].last, 5u);
	EXPECT_EQ(ranges[0].firstVisibleIndex, 0u);
	EXPECT_EQ(ranges[1].first, 8u);
	EXPECT_EQ(ranges[1].last, 9u);
	EXPECT_EQ(ranges[1].firstVisibleIndex, 6u);
	EXPECT_EQ(textfilter::CountVisibleLines(ranges), 8u);
	EXPECT_TRUE(textfilter::BuildVisibleRanges({}, 10, 3).empty());
}

TEST(RealtimeTextFilter, MapsVirtualRowsToSourceLinesWithoutLinearRangeWalk)
{
	const auto ranges = textfilter::BuildVisibleRanges({ 1, 4, 9 }, 10, 1);
	EXPECT_EQ(textfilter::SourceLineForVisibleIndex(ranges, 0), 0u);
	EXPECT_EQ(textfilter::SourceLineForVisibleIndex(ranges, 5), 5u);
	EXPECT_EQ(textfilter::SourceLineForVisibleIndex(ranges, 6), 8u);
	EXPECT_EQ(textfilter::SourceLineForVisibleIndex(ranges, 7), 9u);
	EXPECT_FALSE(textfilter::SourceLineForVisibleIndex(ranges, 8).has_value());
}

TEST(RealtimeTextFilter, FingerprintsAreStableAndDetectChangedLines)
{
	EXPECT_EQ(textfilter::FingerprintLine(L"same line"), textfilter::FingerprintLine(L"same line"));
	EXPECT_NE(textfilter::FingerprintLine(L"same line"), textfilter::FingerprintLine(L"changed line"));
	EXPECT_NE(textfilter::FingerprintLine(L""), textfilter::FingerprintLine(L" "));
	EXPECT_NE(textfilter::FingerprintLine(L"tail", false), textfilter::FingerprintLine(L"tail", true));
}

TEST(RealtimeTextFilter, ProcessesVersionTaggedBatchesAsynchronously)
{
	std::mutex mutex;
	std::condition_variable ready;
	std::optional<textfilter::BatchResult> completed;
	textfilter::Service service;
	textfilter::Options options;
	options.includeTerms = { L"match" };
	service.Begin(17, 41, std::move(options), [&](textfilter::BatchResult result) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			completed = std::move(result);
		}
		ready.notify_one();
	});
	ASSERT_TRUE(service.Submit(17, {
		{ 0, L"first" }, { 1, L"a match" }, { 2, L"second match" }
	}, true));

	std::unique_lock<std::mutex> lock(mutex);
	ASSERT_TRUE(ready.wait_for(lock, std::chrono::seconds(5), [&] { return completed.has_value(); }));
	EXPECT_EQ(completed->requestToken, 17u);
	EXPECT_EQ(completed->documentVersion, 41u);
	EXPECT_TRUE(completed->finalBatch);
	EXPECT_FALSE(completed->failed);
	EXPECT_EQ(completed->matchingLines, (std::vector<std::size_t>{ 1, 2 }));
}

TEST(RealtimeTextFilter, KeepsMillionLineRangeMappingCompact)
{
	std::vector<std::size_t> matches;
	matches.reserve(1000000);
	for (std::size_t line = 0; line < 1000000; ++line) matches.push_back(line);
	const auto ranges = textfilter::BuildVisibleRanges(matches, 1000000, 0);
	ASSERT_EQ(ranges.size(), 1u);
	EXPECT_EQ(ranges.front().first, 0u);
	EXPECT_EQ(ranges.front().last, 999999u);
	EXPECT_EQ(textfilter::CountVisibleLines(ranges), 1000000u);
}

TEST(RealtimeTextFilter, HandlesManyScatteredMatchesInLargeDocuments)
{
	std::vector<std::size_t> matches;
	matches.reserve(100000);
	for (std::size_t line = 0; line < 200000; line += 2) matches.push_back(line);
	const auto ranges = textfilter::BuildVisibleRanges(matches, 200000, 0);
	ASSERT_EQ(ranges.size(), 100000u);
	EXPECT_EQ(textfilter::CountVisibleLines(ranges), 100000u);
	EXPECT_EQ(textfilter::SourceLineForVisibleIndex(ranges, 99999), 199998u);
}
