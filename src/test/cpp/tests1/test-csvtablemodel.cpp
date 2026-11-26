/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "parse/CSVTableModel.h"

#include <utility>

namespace {
std::wstring ApplyCsvPatches(std::wstring text, const csvtable::TextEditBatch& patches)
{
	for (const auto& patch : patches) {
		if (!patch.valid || patch.begin > text.size() || patch.deleteLength > text.size() - patch.begin) return {};
		text.replace(patch.begin, patch.deleteLength, patch.replacement);
	}
	return text;
}
}

TEST(CSVTableModel, ExposesHeaderAndVirtualColumnsWithoutChangingSource)
{
	const std::wstring source = L"part,value\n000123,\"A,B\"\n";
	auto snapshot = csvtable::CSVTableSnapshot::Create(source, {}, 42);
	csvtable::CSVTableModel table(snapshot, true);
	EXPECT_EQ(table.RowCount(), 1u);
	EXPECT_EQ(table.ColumnCount(), 2u);
	EXPECT_EQ(table.ColumnTitle(0), L"part");
	EXPECT_EQ(table.CellText(0, 0), L"000123");
	EXPECT_EQ(table.CellText(0, 1), L"A,B");
	EXPECT_EQ(snapshot->Text(), source);
	EXPECT_EQ(table.DocumentVersion(), 42u);
}

TEST(CSVTableModel, GeneratesVirtualColumnTitlesWhenHeaderIsAbsent)
{
	auto snapshot = csvtable::CSVTableSnapshot::Create(L"a,b\n1,2");
	csvtable::CSVTableModel table(snapshot, false);
	EXPECT_EQ(table.RowCount(), 2u);
	EXPECT_EQ(table.ColumnTitle(0), L"Column 1");
	EXPECT_EQ(table.ColumnTitle(1), L"Column 2");
	EXPECT_EQ(table.CellText(1, 1), L"2");
}

TEST(CSVTableModel, ReturnsOriginalFieldRangesAndHandlesUnevenRows)
{
	const std::wstring source = L"a,b\n1,\"two\"\n3\n";
	auto snapshot = csvtable::CSVTableSnapshot::Create(source);
	csvtable::CSVTableModel table(snapshot, true);
	const auto range = table.CellRawRange(0, 1);
	ASSERT_TRUE(range.valid);
	EXPECT_EQ(source.substr(range.begin, range.length), L"\"two\"");
	EXPECT_FALSE(table.CellRawRange(1, 1).valid);
	EXPECT_TRUE(table.CellText(1, 1).empty());
	EXPECT_EQ(table.FirstPhysicalLine(1), 3u);
}

TEST(CSVTableModel, PreparesAFieldOnlyPatchAndLeavesOtherSourceBytesAlone)
{
	const std::wstring source = L"id,value\r\n1,old\r\n2,keep\r\n";
	auto snapshot = csvtable::CSVTableSnapshot::Create(source, {}, 7);
	csvtable::CSVTableModel table(snapshot, true);
	const auto patch = table.PrepareCellEdit(0, 1, L"new,value");
	ASSERT_TRUE(patch.valid);
	EXPECT_EQ(patch.documentVersion, 7u);
	std::wstring edited = source;
	edited.replace(patch.begin, patch.deleteLength, patch.replacement);
	EXPECT_EQ(edited, L"id,value\r\n1,\"new,value\"\r\n2,keep\r\n");
	EXPECT_EQ(snapshot->Text(), source);
	EXPECT_FALSE(table.PrepareCellEdit(1, 2, L"not present").valid);
}

TEST(CSVTableModel, KeepsQuotedMultilineCellsAsSingleDisplayValues)
{
	auto snapshot = csvtable::CSVTableSnapshot::Create(L"id,note\n1,\"line one\r\nline two\"\n");
	csvtable::CSVTableModel table(snapshot, true);
	EXPECT_EQ(table.RowCount(), 1u);
	EXPECT_EQ(table.FirstPhysicalLine(0), 2u);
	EXPECT_EQ(table.CellText(0, 1), L"line one\r\nline two");
}

TEST(CSVTableModel, PreparesAppendAndDeleteRowPatchesWithoutReformattingExistingRows)
{
	const std::wstring source = L"id,note\r\n1,keep  \r\n";
	auto snapshot = csvtable::CSVTableSnapshot::Create(source);
	csvtable::CSVTableModel table(snapshot, true);
	const auto append = table.PrepareAppendRow({ L"2", L"new, value" });
	ASSERT_TRUE(append.valid);
	EXPECT_EQ(append.documentVersion, table.DocumentVersion());
	std::wstring appended = source;
	appended.replace(append.begin, append.deleteLength, append.replacement);
	EXPECT_EQ(appended, L"id,note\r\n1,keep  \r\n2,\"new, value\"");
	auto appendedSnapshot = csvtable::CSVTableSnapshot::Create(appended);
	csvtable::CSVTableModel appendedTable(appendedSnapshot, true);
	EXPECT_EQ(appendedTable.CellText(1, 1), L"new, value");

	const auto remove = table.PrepareDeleteRow(0);
	ASSERT_TRUE(remove.valid);
	std::wstring deleted = source;
	deleted.replace(remove.begin, remove.deleteLength, remove.replacement);
	EXPECT_EQ(deleted, L"id,note\r\n");
}

TEST(CSVTableModel, PreparesDescendingColumnPatchesForUnevenRecords)
{
	const std::wstring source = L"a,b,c\n1,2,3\n4,5\n";
	auto snapshot = csvtable::CSVTableSnapshot::Create(source);
	csvtable::CSVTableModel table(snapshot, false);
	const auto append = table.PrepareAppendColumn();
	ASSERT_EQ(append.size(), 3u);
	EXPECT_GT(append[0].begin, append[1].begin);
	EXPECT_GT(append[1].begin, append[2].begin);
	EXPECT_EQ(append[0].replacement, L",");
	const std::wstring appended = ApplyCsvPatches(source, append);
	EXPECT_EQ(appended, L"a,b,c,\n1,2,3,\n4,5,\n");

	const auto remove = table.PrepareDeleteColumn(1);
	ASSERT_EQ(remove.size(), 3u);
	EXPECT_GT(remove[0].begin, remove[1].begin);
	EXPECT_GT(remove[1].begin, remove[2].begin);
	EXPECT_EQ(source.substr(remove[2].begin, remove[2].deleteLength), L",b");
	const std::wstring deleted = ApplyCsvPatches(source, remove);
	EXPECT_EQ(deleted, L"a,c\n1,3\n4\n");
	auto deletedSnapshot = csvtable::CSVTableSnapshot::Create(deleted);
	csvtable::CSVTableModel deletedTable(deletedSnapshot, false);
	EXPECT_EQ(deletedTable.ColumnCount(), 2u);
	EXPECT_EQ(deletedTable.CellText(2, 0), L"4");
}

TEST(CSVTableModel, RefusesAppendingARecordWhenTheSourceEndsInsideAnUnclosedQuote)
{
	auto snapshot = csvtable::CSVTableSnapshot::Create(L"name,note\n1,\"not closed");
	csvtable::CSVTableModel table(snapshot, true);
	EXPECT_FALSE(table.PrepareAppendRow({ L"2", L"new row" }).valid);
}

TEST(CSVTableModel, AppliesColumnFiltersWithAndOrAndRegexValidation)
{
	auto snapshot = csvtable::CSVTableSnapshot::Create(L"name,amount\nAlpha,10\nBeta,2\nGamma,\n");
	csvtable::CSVTableModel table(snapshot, true);
	const std::vector<csvtable::Filter> andFilters = {
		{ 0, csvtable::FilterOperator::Contains, L"a", false },
		{ 1, csvtable::FilterOperator::NumberGreater, L"3", false },
	};
	auto result = table.BuildViewOrder(andFilters, csvtable::FilterLogic::And, {});
	ASSERT_FALSE(result.identity);
	ASSERT_EQ(result.rows.size(), 1u);
	EXPECT_EQ(result.rows[0], 0u);

	const std::vector<csvtable::Filter> orFilters = {
		{ 0, csvtable::FilterOperator::Equals, L"Beta", true },
		{ 1, csvtable::FilterOperator::Empty, {}, false },
	};
	result = table.BuildViewOrder(orFilters, csvtable::FilterLogic::Or, {});
	ASSERT_EQ(result.rows.size(), 2u);
	EXPECT_EQ(result.rows[0], 1u);
	EXPECT_EQ(result.rows[1], 2u);

	const std::vector<csvtable::Filter> badRegex = { { 0, csvtable::FilterOperator::Regex, L"[", false } };
	result = table.BuildViewOrder(badRegex, csvtable::FilterLogic::And, {});
	EXPECT_EQ(result.invalidFilter, 0u);
}

TEST(CSVTableModel, SupportsTextFilterVariantsAndRejectsOutOfRangeColumns)
{
	auto snapshot = csvtable::CSVTableSnapshot::Create(L"name\nAlpha\nBeta\nGamma\n\n");
	csvtable::CSVTableModel table(snapshot, true);
	auto result = table.BuildViewOrder({ { 0, csvtable::FilterOperator::NotContains, L"ph", false } },
		csvtable::FilterLogic::And, {});
	ASSERT_EQ(result.rows.size(), 3u);
	EXPECT_EQ(table.CellText(result.rows[0], 0), L"Beta");
	EXPECT_EQ(table.CellText(result.rows[1], 0), L"Gamma");
	EXPECT_TRUE(table.CellText(result.rows[2], 0).empty());

	result = table.BuildViewOrder({ { 0, csvtable::FilterOperator::StartsWith, L"ga", false } },
		csvtable::FilterLogic::And, {});
	ASSERT_EQ(result.rows.size(), 1u);
	EXPECT_EQ(table.CellText(result.rows[0], 0), L"Gamma");

	result = table.BuildViewOrder({ { 0, csvtable::FilterOperator::EndsWith, L"ta", true } },
		csvtable::FilterLogic::And, {});
	ASSERT_EQ(result.rows.size(), 1u);
	EXPECT_EQ(table.CellText(result.rows[0], 0), L"Beta");

	result = table.BuildViewOrder({ { 0, csvtable::FilterOperator::NonEmpty, {}, false } },
		csvtable::FilterLogic::And, {});
	ASSERT_EQ(result.rows.size(), 3u);

	result = table.BuildViewOrder({ { 1, csvtable::FilterOperator::Empty, {}, false } },
		csvtable::FilterLogic::And, {});
	EXPECT_EQ(result.invalidFilter, 0u);
}

TEST(CSVTableModel, SortsStringNumericDateNaturalAndMultipleColumns)
{
	{
		auto snapshot = csvtable::CSVTableSnapshot::Create(L"group,name\nx,b\nx,a\ny,z\n");
		csvtable::CSVTableModel table(snapshot, true);
		auto result = table.BuildViewOrder({}, csvtable::FilterLogic::And, {
			{ 0, csvtable::SortKind::String, false }, { 1, csvtable::SortKind::String, false } });
		ASSERT_EQ(result.rows.size(), 3u);
		EXPECT_EQ(table.CellText(result.rows[0], 1), L"a");
		EXPECT_EQ(table.CellText(result.rows[1], 1), L"b");
		EXPECT_EQ(table.CellText(result.rows[2], 1), L"z");
	}
	{
		auto snapshot = csvtable::CSVTableSnapshot::Create(L"value\n10\n2\n000123\n");
		csvtable::CSVTableModel table(snapshot, true);
		auto result = table.BuildViewOrder({}, csvtable::FilterLogic::And, { { 0, csvtable::SortKind::Numeric, false } });
		ASSERT_EQ(result.rows.size(), 3u);
		EXPECT_EQ(table.CellText(result.rows[0], 0), L"2");
		EXPECT_EQ(table.CellText(result.rows[1], 0), L"10");
		EXPECT_EQ(table.CellText(result.rows[2], 0), L"000123");
	}
	{
		auto snapshot = csvtable::CSVTableSnapshot::Create(L"name\nfile10\nfile2\nfile02\n");
		csvtable::CSVTableModel table(snapshot, true);
		auto result = table.BuildViewOrder({}, csvtable::FilterLogic::And, { { 0, csvtable::SortKind::Natural, false } });
		ASSERT_EQ(result.rows.size(), 3u);
		EXPECT_EQ(table.CellText(result.rows[0], 0), L"file2");
		EXPECT_EQ(table.CellText(result.rows[1], 0), L"file02");
		EXPECT_EQ(table.CellText(result.rows[2], 0), L"file10");
	}
	{
		auto snapshot = csvtable::CSVTableSnapshot::Create(L"date,name\n2024-10-01,b\n2024/02/03,a\n2023-12-01,z\n");
		csvtable::CSVTableModel table(snapshot, true);
		auto result = table.BuildViewOrder({}, csvtable::FilterLogic::And, {
			{ 0, csvtable::SortKind::Date, false }, { 1, csvtable::SortKind::String, false } });
		ASSERT_EQ(result.rows.size(), 3u);
		EXPECT_EQ(table.CellText(result.rows[0], 0), L"2023-12-01");
		EXPECT_EQ(table.CellText(result.rows[1], 0), L"2024/02/03");
		EXPECT_EQ(table.CellText(result.rows[2], 0), L"2024-10-01");
	}
}

TEST(CSVTableModel, CancelsLongFilterAndSortJobs)
{
	std::wstring text = L"value\n";
	for (int i = 0; i < 1000; ++i) text += std::to_wstring(1000 - i) + L"\n";
	auto snapshot = csvtable::CSVTableSnapshot::Create(std::move(text));
	csvtable::CSVTableModel table(snapshot, true);
	int checks = 0;
	auto cancelled = [&checks]() { return ++checks > 1; };
	auto result = table.BuildViewOrder({ { 0, csvtable::FilterOperator::Contains, L"1", false } },
		csvtable::FilterLogic::And, {}, cancelled);
	EXPECT_TRUE(result.cancelled);

	checks = 0;
	result = table.BuildViewOrder({}, csvtable::FilterLogic::And,
		{ { 0, csvtable::SortKind::Numeric, false } }, cancelled);
	EXPECT_TRUE(result.cancelled);
	EXPECT_TRUE(result.rows.empty());
}
