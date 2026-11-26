/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "view/colors/CWordMarker.h"
#include <chrono>

TEST(CWordMarker, ToggleAndMatch)
{
	auto& markers = CWordMarker::Instance();
	markers.Load({});
	EXPECT_TRUE(markers.Toggle(L"alpha", 0x00aabbcc));
	EXPECT_FALSE(markers.Toggle(L"two\nlines", 1));
	EXPECT_FALSE(markers.Toggle(std::wstring(L"nul\0word", 8), 1));
	int end = -1;
	EXPECT_EQ(markers.Match(nullptr, L"alpha beta", 10, 0, &end), 1);
	EXPECT_EQ(end, 5);
	EXPECT_EQ(markers.Match(nullptr, L"xalpha", 6, 1, &end), 0);
	EXPECT_EQ(markers.Match(nullptr, L" alpha", 6, 1, &end), 1);
	EXPECT_EQ(end, 6);
	EXPECT_EQ(markers.Match(nullptr, L"beta", 4, 0, &end), 0);
	markers.Load({ { L"off", 1, false }, { L"on", 2, true } });
	EXPECT_EQ(markers.Match(nullptr, L"off on", 6, 0, &end), 0);
	EXPECT_EQ(markers.Match(nullptr, L"off on", 6, 4, &end), 2);
	EXPECT_TRUE(markers.ToggleEnabled(0));
	EXPECT_EQ(markers.Match(nullptr, L"off on", 6, 0, &end), 1);
	const int document = 0;
	markers.SetAutomatic(&document, L"beta");
	markers.ClearFixed();
	EXPECT_EQ(markers.Match(&document, L"beta", 4, 0, &end), 33);
	markers.Load({});
	EXPECT_TRUE(markers.Toggle(L"alpha", 0));
	EXPECT_TRUE(markers.Toggle(L"alpha", 0));
	EXPECT_TRUE(markers.Entries().empty());
	markers.Load({});
}

TEST(CWordMarker, LoadIsBounded)
{
	std::vector<CWordMarker::Entry> entries;
	for (int i = 0; i < 40; ++i) entries.push_back({ std::to_wstring(i), static_cast<unsigned>(i) });
	entries.push_back({ L"invalid\nmarker", 40 });
	entries.push_back({ L"0", 41 }); // entries after the 32-item limit are ignored
	auto& markers = CWordMarker::Instance();
	markers.Load(entries);
	EXPECT_EQ(markers.Entries().size(), 32u);
	EXPECT_EQ(markers.Entries().front().word, L"0");
	EXPECT_EQ(markers.Entries().back().word, L"31");
	markers.Load({});
}

TEST(CWordMarker, ChoosesUnusedPaletteColorsFirst)
{
	auto& markers = CWordMarker::Instance();
	markers.Load({ { L"one", 0 }, { L"two", 2 } });
	const std::vector<unsigned> palette{ 0, 1, 2, 3 };
	EXPECT_EQ(markers.NextColor(palette), 1u);
	markers.Load({ { L"one", 0 }, { L"two", 1 }, { L"three", 2 }, { L"four", 3 } });
	const auto extra = markers.NextColor(palette);
	EXPECT_TRUE(CWordMarker::IsCustomColor(extra));
	EXPECT_NE(CWordMarker::DecodeCustomColor(extra), 0u);
	markers.Load({});
	EXPECT_TRUE(CWordMarker::IsCustomColor(markers.NextColor(std::vector<unsigned>{ 0 })));
	markers.Load({});
}

TEST(CWordMarker, DuplicateLoadedColorsAreReassigned)
{
	auto& markers = CWordMarker::Instance();
	markers.Load({ { L"first", 1 }, { L"second", 1 }, { L"third", 1 } });
	ASSERT_EQ(markers.Entries().size(), 3u);
	EXPECT_EQ(markers.Entries()[0].color, 1u);
	EXPECT_TRUE(CWordMarker::IsCustomColor(markers.Entries()[1].color));
	EXPECT_TRUE(CWordMarker::IsCustomColor(markers.Entries()[2].color));
	EXPECT_NE(markers.Entries()[1].color, markers.Entries()[2].color);
	markers.Load({});
}

TEST(CWordMarker, PrefersLongestOverlappingMarker)
{
	auto& markers = CWordMarker::Instance();
	markers.Load({ { L"word", 0 }, { L"wording", 1 } });
	int end = -1;
	EXPECT_EQ(markers.Match(nullptr, L"wording", 7, 0, &end), 2);
	EXPECT_EQ(end, 7);
	EXPECT_EQ(markers.Match(nullptr, L"word ", 5, 0, &end), 1);
	EXPECT_EQ(end, 4);
	markers.Load({ { L"x", 0 } });
	EXPECT_EQ(markers.Match(nullptr, L"xabc", 4, 0, &end), 0);
	EXPECT_EQ(markers.Match(nullptr, L" x ", 3, 1, &end), 1);
	EXPECT_EQ(end, 2);
	markers.Load({});
}

TEST(CWordMarker, AutomaticMarkerMatchesCurrentWord)
{
	auto& markers = CWordMarker::Instance();
	markers.Load({});
	const int document = 1;
	markers.SetAutomatic(&document, L"beta");
	ASSERT_NE(markers.AutomaticWord(&document), nullptr);
	EXPECT_EQ(*markers.AutomaticWord(&document), L"beta");
	int end = -1;
	EXPECT_EQ(markers.Match(&document, L"alpha beta", 10, 6, &end), 33);
	EXPECT_EQ(end, 10);
	EXPECT_EQ(markers.Match(&document, L"alphabetabetaz", 14, 5, &end), 33);
	EXPECT_EQ(end, 9);
	const int otherDocument = 2;
	EXPECT_EQ(markers.Match(&otherDocument, L"alpha beta", 10, 6, &end), 0);
	markers.SetAutomaticEnabled(false);
	EXPECT_EQ(markers.Match(&document, L"alpha beta", 10, 6, &end), 0);
	markers.SetAutomaticEnabled(true);
	EXPECT_EQ(markers.Match(nullptr, L"alpha beta", 10, 0, &end), 0);
	markers.ClearAutomatic(&document);
	EXPECT_EQ(markers.AutomaticWord(&document), nullptr);
	EXPECT_EQ(markers.Match(&document, L"alpha beta", 10, 6, &end), 0);
	markers.Load({});
}

TEST(CWordMarker, LargeLineScan)
{
	std::vector<CWordMarker::Entry> entries;
	for (int i = 0; i < 32; ++i) entries.push_back({ L"marker" + std::to_wstring(i), static_cast<unsigned>(i) });
	auto& markers = CWordMarker::Instance();
	markers.Load(entries);
	std::wstring line(1024 * 1024, L'x');
	const std::wstring target = L"marker31";
	const int targetPosition = static_cast<int>(line.size()) - static_cast<int>(target.size());
	line.replace(targetPosition, target.size(), target);
	line[targetPosition - 1] = L' ';
	int matches = 0;
	int end = -1;
	const auto start = std::chrono::steady_clock::now();
	for (int position = 0; position < static_cast<int>(line.size()); ++position) {
		if (markers.Match(nullptr, line.data(), static_cast<int>(line.size()), position, &end) != 0) ++matches;
	}
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	RecordProperty("one_mib_scan_ms", static_cast<int>(elapsed));
	EXPECT_EQ(matches, 1);
	EXPECT_EQ(end, static_cast<int>(line.size()));
	markers.Load({});
}
