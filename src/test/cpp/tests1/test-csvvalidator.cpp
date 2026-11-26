#include <gtest/gtest.h>

#include "parse/CSVParser.h"
#include "parse/CSVValidator.h"

#include <algorithm>

TEST(CSVValidator, ReportsRaggedRecordAndUnclosedQuoteWithLocations)
{
	const std::wstring text = L"name,value\r\nalpha,1\r\nbeta\r\ngamma,\"unterminated";
	const auto parsed = csvtable::Parse(text);
	const auto diagnostics = csvtable::Validate(text, parsed);
	ASSERT_EQ(diagnostics.size(), 2u);
	EXPECT_EQ(diagnostics[0].code, L"CSV010");
	EXPECT_EQ(diagnostics[0].record, 3u);
	EXPECT_EQ(diagnostics[0].physicalLine, 3u);
	EXPECT_EQ(diagnostics[1].code, L"CSV003");
	EXPECT_EQ(diagnostics[1].record, 4u);
	EXPECT_EQ(diagnostics[1].physicalLine, 4u);
}

TEST(CSVValidator, ChecksDuplicateAndEmptyHeadersAndRequiredFields)
{
	const std::wstring text = L"id,id,\n1,\n";
	const auto parsed = csvtable::Parse(text);
	csvtable::ValidationOptions options;
	options.hasHeader = true;
	options.requiredColumns = { 1 };
	const auto diagnostics = csvtable::Validate(text, parsed, options);
	EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
		return item.code == L"CSV011";
	}));
	EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
		return item.code == L"CSV012";
	}));
	EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
		return item.code == L"CSV013";
	}));
}
