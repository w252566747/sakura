/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include <gtest/gtest.h>

#include "parse/CSVStatistics.h"

TEST(CSVStatistics, IgnoresNonNumericTextAndPreservesLeadingZeroIdentifiers)
{
	auto snapshot = csvtable::CSVTableSnapshot::Create(L"id,amount\n000123,10\nA-2,not-number\nX,2\n");
	csvtable::CSVTableModel table(snapshot, true);
	const auto stats = csvtable::CalculateStatistics(table, {
		{ 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 }, { 2, 1 }, { 99, 99 }
	});

	EXPECT_EQ(stats.selectedCount, 5u);
	EXPECT_EQ(stats.numericCount, 2u);
	ASSERT_TRUE(stats.sum.has_value());
	ASSERT_TRUE(stats.average.has_value());
	ASSERT_TRUE(stats.minimum.has_value());
	ASSERT_TRUE(stats.maximum.has_value());
	EXPECT_DOUBLE_EQ(static_cast<double>(*stats.sum), 12.0);
	EXPECT_DOUBLE_EQ(static_cast<double>(*stats.average), 6.0);
	EXPECT_DOUBLE_EQ(static_cast<double>(*stats.minimum), 2.0);
	EXPECT_DOUBLE_EQ(static_cast<double>(*stats.maximum), 10.0);
	EXPECT_EQ(table.CellText(0, 0), L"000123");
}

TEST(CSVStatistics, LeavesAggregatesEmptyWhenNoNumbersAreSelected)
{
	auto snapshot = csvtable::CSVTableSnapshot::Create(L"value\n\nN/A\n");
	csvtable::CSVTableModel table(snapshot, true);
	const auto stats = csvtable::CalculateStatistics(table, { { 0, 0 }, { 1, 0 } });

	EXPECT_EQ(stats.selectedCount, 2u);
	EXPECT_EQ(stats.numericCount, 0u);
	EXPECT_FALSE(stats.sum.has_value());
	EXPECT_FALSE(stats.average.has_value());
	EXPECT_FALSE(stats.minimum.has_value());
	EXPECT_FALSE(stats.maximum.has_value());
}

TEST(CSVStatistics, DoesNotConvertLongNumericIdentifiersOrScientificNotationInSource)
{
	auto snapshot = csvtable::CSVTableSnapshot::Create(L"value\n12345678901234567890\n1.5\n");
	csvtable::CSVTableModel table(snapshot, true);
	const auto stats = csvtable::CalculateStatistics(table, { { 0, 0 }, { 1, 0 } });

	EXPECT_EQ(stats.numericCount, 1u);
	ASSERT_TRUE(stats.sum.has_value());
	EXPECT_DOUBLE_EQ(static_cast<double>(*stats.sum), 1.5);
	EXPECT_EQ(table.CellText(0, 0), L"12345678901234567890");
}
