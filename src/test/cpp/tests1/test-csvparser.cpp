/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "parse/CSVParser.h"
#include "parse/CSVValidator.h"

#include <algorithm>

TEST(CSVParser, ParsesQuotedDelimitersEscapedQuotesAndTrailingEmptyFields)
{
	const std::wstring text = L"name,note,empty\r\nA,\"one, two \"\"quoted\"\"\",\r\nB,,last";
	const auto parsed = csvtable::Parse(text);
	ASSERT_TRUE(parsed.errors.empty());
	ASSERT_EQ(parsed.records.size(), 3u);
	ASSERT_EQ(parsed.records[1].fieldCount, 3u);
	EXPECT_EQ(csvtable::DecodeField(text, *parsed.FieldAt(parsed.records[1], 1)), L"one, two \"quoted\"");
	EXPECT_TRUE(csvtable::DecodeField(text, *parsed.FieldAt(parsed.records[1], 2)).empty());
	EXPECT_EQ(csvtable::DecodeField(text, *parsed.FieldAt(parsed.records[2], 1)), L"");
	EXPECT_EQ(csvtable::DecodeField(text, *parsed.FieldAt(parsed.records[2], 2)), L"last");
	EXPECT_EQ(parsed.records[0].rawLength, 17u); // includes CRLF
}

TEST(CSVParser, KeepsMultilineQuotedFieldsInsideOneLogicalRecord)
{
	const std::wstring text = L"1,\"first\r\nsecond\nthird\",x\r\n2,y,z\n";
	const auto parsed = csvtable::Parse(text);
	ASSERT_TRUE(parsed.errors.empty());
	ASSERT_EQ(parsed.records.size(), 2u);
	EXPECT_EQ(parsed.records[0].firstPhysicalLine, 1u);
	EXPECT_EQ(parsed.records[0].physicalLineCount, 3u);
	EXPECT_EQ(parsed.records[1].firstPhysicalLine, 4u);
	EXPECT_EQ(csvtable::DecodeField(text, *parsed.FieldAt(parsed.records[0], 1)), L"first\r\nsecond\nthird");
}

TEST(CSVParser, SupportsAlternateAndCustomDelimitersAndDetection)
{
	const std::wstring text = L"key;value\r\none;two\r\nthree;four";
	EXPECT_EQ(csvtable::DetectDelimiter(text), L';');
	const auto parsed = csvtable::Parse(text, { L';', true });
	ASSERT_EQ(parsed.records.size(), 3u);
	EXPECT_EQ(parsed.records[1].fieldCount, 2u);

	const auto pipe = csvtable::Parse(L"a|b\n1|2", { L'|', true });
	ASSERT_EQ(pipe.records.size(), 2u);
	EXPECT_EQ(csvtable::DecodeField(L"a|b\n1|2", *pipe.FieldAt(pipe.records[1], 1)), L"2");
}

TEST(CSVParser, DelimiterDetectionIgnoresSeparatorsInsideQuotes)
{
	const std::wstring text = L"name,note\r\nx,\"a;b|c\"\r\ny,\"d;e|f\"";
	EXPECT_EQ(csvtable::DetectDelimiter(text), L',');
}

TEST(CSVParser, AcceptsEnterpriseWhitespaceAfterClosingQuote)
{
	const std::wstring text = L"a,\" value \"  ,c";
	const auto parsed = csvtable::Parse(text);
	ASSERT_TRUE(parsed.errors.empty());
	ASSERT_EQ(parsed.records.size(), 1u);
	EXPECT_EQ(csvtable::DecodeField(text, *parsed.FieldAt(parsed.records[0], 1)), L" value ");
	EXPECT_EQ(text.substr(parsed.FieldAt(parsed.records[0], 1)->rawBegin,
		parsed.FieldAt(parsed.records[0], 1)->rawLength), L"\" value \"  ");
}

TEST(CSVParser, SerializesOnlyWhenQuotingIsRequired)
{
	EXPECT_EQ(csvtable::SerializeField(L"plain", L','), std::optional<std::wstring>(L"plain"));
	EXPECT_EQ(csvtable::SerializeField(L"a,b", L','), std::optional<std::wstring>(L"\"a,b\""));
	EXPECT_EQ(csvtable::SerializeField(L"a\"b", L','), std::optional<std::wstring>(L"\"a\"\"b\""));
	EXPECT_EQ(csvtable::SerializeField(L" leading", L','), std::optional<std::wstring>(L"\" leading\""));
	EXPECT_EQ(csvtable::SerializeField(L"line1\nline2", L','), std::optional<std::wstring>(L"\"line1\nline2\""));
	EXPECT_FALSE(csvtable::SerializeField(L"a,b", L'"').has_value());
}

TEST(CSVParser, PreservesMalformedFieldsAndReportsTheirLocations)
{
	const std::wstring unclosed = L"id,value\n1,\"not closed\ncontinued";
	const auto first = csvtable::Parse(unclosed);
	ASSERT_EQ(first.records.size(), 2u);
	ASSERT_EQ(first.errors.size(), 1u);
	EXPECT_EQ(first.errors[0].code, csvtable::ErrorCode::UnclosedQuote);
	EXPECT_EQ(first.errors[0].physicalLine, 2u);
	EXPECT_EQ(csvtable::DecodeField(unclosed, *first.FieldAt(first.records[1], 1)), L"\"not closed\ncontinued");

	const std::wstring trailing = L"a,\"ok\"x\n";
	const auto second = csvtable::Parse(trailing);
	ASSERT_EQ(second.errors.size(), 1u);
	EXPECT_EQ(second.errors[0].code, csvtable::ErrorCode::UnexpectedCharacterAfterQuote);
	EXPECT_EQ(csvtable::DecodeField(trailing, *second.FieldAt(second.records[0], 1)), L"\"ok\"x");

	const std::wstring stray = L"a,un\"closed\n";
	const auto third = csvtable::Parse(stray);
	ASSERT_EQ(third.errors.size(), 1u);
	EXPECT_EQ(third.errors[0].code, csvtable::ErrorCode::UnexpectedQuote);
	EXPECT_EQ(third.errors[0].column, 5u);
	EXPECT_EQ(csvtable::DecodeField(stray, *third.FieldAt(third.records[0], 1)), L"un\"closed");
}

TEST(CSVParser, HandlesEmptyLinesLineEndingsAndRecordRanges)
{
	const std::wstring text = L"\r\nx,\n";
	const auto parsed = csvtable::Parse(text);
	ASSERT_EQ(parsed.records.size(), 2u);
	ASSERT_EQ(parsed.records[0].fieldCount, 1u);
	EXPECT_TRUE(csvtable::DecodeField(text, *parsed.FieldAt(parsed.records[0], 0)).empty());
	ASSERT_EQ(parsed.records[1].fieldCount, 2u);
	EXPECT_TRUE(csvtable::DecodeField(text, *parsed.FieldAt(parsed.records[1], 1)).empty());
	EXPECT_EQ(parsed.records[1].rawBegin + parsed.records[1].rawLength, text.size());
}

TEST(CSVParser, RejectsQuoteAndNewlineAsDelimiters)
{
	const auto quote = csvtable::Parse(L"a,b", { L'"', true });
	const auto newline = csvtable::Parse(L"a,b", { L'\n', true });
	EXPECT_TRUE(quote.records.empty());
	ASSERT_EQ(quote.errors.size(), 1u);
	EXPECT_EQ(quote.errors[0].code, csvtable::ErrorCode::InvalidDelimiter);
	EXPECT_TRUE(newline.records.empty());
	ASSERT_EQ(newline.errors.size(), 1u);
	EXPECT_EQ(newline.errors[0].code, csvtable::ErrorCode::InvalidDelimiter);
}

TEST(CSVParser, StopsAtCancellationBoundaryAndMarksPartialResultUnusable)
{
	std::wstring input;
	for (int row = 0; row < 4000; ++row) input += L"one,two,three\n";
	int checks = 0;
	const auto parsed = csvtable::Parse(input, {}, [&checks] { return ++checks > 1; });
	EXPECT_TRUE(parsed.cancelled);
	EXPECT_LT(parsed.records.size(), 4000u);
}

TEST(CSVValidator, ReportsInconsistentColumnsWithLogicalAndPhysicalLocations)
{
	const std::wstring text = L"id,name\r\n1,A\r\n2,B,extra\r\n";
	const auto parsed = csvtable::Parse(text);
	csvtable::ValidationOptions options;
	options.hasHeader = true;
	const auto diagnostics = csvtable::Validate(text, parsed, options);
	const auto found = std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
		return diagnostic.code == L"CSV010";
	});
	ASSERT_NE(found, diagnostics.end());
	EXPECT_EQ(found->severity, csvtable::Severity::Error);
	EXPECT_EQ(found->record, 3u);
	EXPECT_EQ(found->physicalLine, 3u);
	EXPECT_EQ(found->column, 3u);
	EXPECT_EQ(found->columnName, L"Column 3");
}

TEST(CSVValidator, DetectsEmptyAndDuplicateHeaders)
{
	const std::wstring text = L"id,id,\n1,1,\n";
	const auto parsed = csvtable::Parse(text);
	csvtable::ValidationOptions options;
	options.hasHeader = true;
	const auto diagnostics = csvtable::Validate(text, parsed, options);
	EXPECT_NE(std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& d) { return d.code == L"CSV011"; }), diagnostics.end());
	EXPECT_NE(std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& d) { return d.code == L"CSV012"; }), diagnostics.end());
}

TEST(CSVValidator, ChecksRequiredFieldsAndCompositeKeysWithoutNumberCoercion)
{
	const std::wstring text = L"part,site,value\n000123,A,10\n000123,A,20\n000124,B,\n";
	const auto parsed = csvtable::Parse(text);
	csvtable::ValidationOptions options;
	options.hasHeader = true;
	options.requiredColumns = { 2 };
	options.primaryKeyColumns = { 0, 1 };
	const auto diagnostics = csvtable::Validate(text, parsed, options);
	const auto duplicate = std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& d) { return d.code == L"CSV014"; });
	const auto required = std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& d) { return d.code == L"CSV013"; });
	ASSERT_NE(duplicate, diagnostics.end());
	ASSERT_NE(required, diagnostics.end());
	EXPECT_EQ(duplicate->record, 3u);
	EXPECT_EQ(duplicate->columnName, L"part");
	EXPECT_EQ(required->record, 4u);
	EXPECT_EQ(required->physicalLine, 4u);
}

TEST(CSVValidator, AllowsVariableWidthRowsToBeWarnings)
{
	const std::wstring text = L"a,b\n1,2\n3\n";
	const auto parsed = csvtable::Parse(text);
	csvtable::ValidationOptions options;
	options.inconsistentFieldCountIsWarning = true;
	const auto diagnostics = csvtable::Validate(text, parsed, options);
	const auto mismatch = std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& d) { return d.code == L"CSV010"; });
	ASSERT_NE(mismatch, diagnostics.end());
	EXPECT_EQ(mismatch->severity, csvtable::Severity::Warning);
}
