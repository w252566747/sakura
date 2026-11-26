/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "view/CScrollBarMarkerModel.h"

TEST(CScrollBarMarkerModel, MapsLogicalLinesToTrackAndClampsClickPositions)
{
	EXPECT_EQ(CScrollBarMarkerModel::LineAt(0, 101, 1001), 0);
	EXPECT_EQ(CScrollBarMarkerModel::LineAt(100, 101, 1001), 1000);
	EXPECT_EQ(CScrollBarMarkerModel::LineAt(-10, 101, 1001), 0);
	EXPECT_EQ(CScrollBarMarkerModel::LineAt(200, 101, 1001), 1000);
	EXPECT_EQ(CScrollBarMarkerModel::LineAt(4, 1, 1001), 0);
}

TEST(CScrollBarMarkerModel, AggregatesSameKindAndColorAtSamePixel)
{
	CScrollBarMarkerModel model;
	model.SetMarkers({
		{10, CScrollBarMarkerModel::Kind::Search, 0xFFA500, L"検索語", {}},
		{10, CScrollBarMarkerModel::Kind::Search, 0xFFA500, L"検索語", {}},
	});
	const auto ticks = model.BuildTicks(10, 100);
	ASSERT_EQ(ticks.size(), 1u);
	EXPECT_EQ(ticks[0].count, 2u);
	EXPECT_EQ(ticks[0].firstLine, 10);
	EXPECT_EQ(ticks[0].lastLine, 10);
	EXPECT_NE(ticks[0].tooltip.find(L"検索語"), std::wstring::npos);
}

TEST(CScrollBarMarkerModel, KeepsOverlappingKindsAndColorsSeparate)
{
	CScrollBarMarkerModel model;
	model.SetMarkers({
		{5, CScrollBarMarkerModel::Kind::WordMarker, 0x33CCFF, L"固定語", {}},
		{5, CScrollBarMarkerModel::Kind::SyntaxError, 0xFF0000, L"構文エラー", L"詳細"},
		{5, CScrollBarMarkerModel::Kind::WordMarker, 0x33CCFF, L"固定語", {}},
	});
	const auto ticks = model.BuildTicks(100, 10);
	ASSERT_EQ(ticks.size(), 2u);
	EXPECT_EQ(ticks[0].count, 2u);
	EXPECT_EQ(ticks[1].count, 1u);
	EXPECT_NE(ticks[0].lane, ticks[1].lane);
	EXPECT_EQ(CScrollBarMarkerModel::HitTestLane(ticks, ticks[1].lane, ticks[1].y), 1u);
	EXPECT_NE(ticks[1].tooltip.find(L"詳細"), std::wstring::npos);
}

TEST(CScrollBarMarkerModel, IgnoresOutOfRangeLinesAndHitTestsNearestTick)
{
	CScrollBarMarkerModel model;
	model.SetMarkers({
		{-1, CScrollBarMarkerModel::Kind::Bookmark, 0x0000FF, {}, {}},
		{50, CScrollBarMarkerModel::Kind::Bookmark, 0x0000FF, {}, {}},
		{101, CScrollBarMarkerModel::Kind::Bookmark, 0x0000FF, {}, {}},
	});
	const auto ticks = model.BuildTicks(101, 101);
	ASSERT_EQ(ticks.size(), 1u);
	EXPECT_EQ(ticks[0].y, 50);
	EXPECT_EQ(CScrollBarMarkerModel::HitTest(ticks, 52), 0u);
	EXPECT_EQ(CScrollBarMarkerModel::HitTest(ticks, 54), ticks.size());
}

TEST(CScrollBarMarkerModel, HandlesLargeLogicalFilesWithoutIntegerOverflow)
{
	CScrollBarMarkerModel model;
	model.SetMarkers({
		{1'999'999'999, CScrollBarMarkerModel::Kind::Modified, 0x008000, {}, {}},
	});
	const auto ticks = model.BuildTicks(20'000, 2'000'000'000);
	ASSERT_EQ(ticks.size(), 1u);
	EXPECT_EQ(ticks[0].y, 19'999);
}

TEST(CScrollBarMarkerModel, CompactOverviewKeepsMemoryBoundedByTrackAndColor)
{
	CScrollBarMarkerModel model;
	model.ResetOverview(20, 1'000'000);
	for (int line = 0; line < 1'000'000; ++line) {
		model.AddOverviewMarker({line, CScrollBarMarkerModel::Kind::Search, 0x00FFA500, L"検索", {}});
	}
	const auto& ticks = model.OverviewTicks();
	EXPECT_LE(ticks.size(), 20u);
	ASSERT_FALSE(ticks.empty());
	EXPECT_EQ(ticks.front().firstLine, 0);
	EXPECT_EQ(ticks.back().lastLine, 999'999);
	size_t total = 0;
	for (const auto& tick : ticks) total += tick.count;
	EXPECT_EQ(total, 1'000'000u);
	EXPECT_EQ(CScrollBarMarkerModel::HitTest(ticks, 19, 0), ticks.size() - 1);
}

TEST(CScrollBarMarkerModel, OverviewKeepsDifferentSourcesInSeparateLanes)
{
	CScrollBarMarkerModel model;
	model.ResetOverview(100, 100);
	model.AddOverviewMarker({25, CScrollBarMarkerModel::Kind::Search, 0x00FFA500, L"検索", L"行 26"});
	model.AddOverviewMarker({25, CScrollBarMarkerModel::Kind::SyntaxError, 0x000000FF, L"Curl Error", L"行 26"});
	const auto& ticks = model.OverviewTicks();
	ASSERT_EQ(ticks.size(), 2u);
	EXPECT_NE(ticks[0].lane, ticks[1].lane);
	EXPECT_NE(CScrollBarMarkerModel::HitTestLane(ticks, ticks[0].lane, ticks[0].y), ticks.size());
	EXPECT_NE(CScrollBarMarkerModel::HitTestLane(ticks, ticks[1].lane, ticks[1].y), ticks.size());
}
