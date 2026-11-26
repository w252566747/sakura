/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CSVPARSER_H_
#define SAKURA_CSVPARSER_H_
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace csvtable {
	enum class ErrorCode {
		InvalidDelimiter,
		UnexpectedQuote,
		UnexpectedCharacterAfterQuote,
		UnclosedQuote,
	};

	struct Field {
		std::size_t rawBegin = 0;
		std::size_t rawLength = 0;
		std::size_t valueBegin = 0;
		std::size_t valueLength = 0;
		bool quoted = false;
		bool malformed = false;
	};

	struct Record {
		std::size_t rawBegin = 0;
		std::size_t rawLength = 0; // Includes the record line ending, if present.
		std::size_t firstPhysicalLine = 1; // One-based.
		std::size_t physicalLineCount = 1;
		std::size_t firstField = 0;
		std::size_t fieldCount = 0;
		bool malformed = false;
	};

	struct ParseError {
		ErrorCode code = ErrorCode::UnexpectedQuote;
		std::size_t offset = 0; // wchar_t offset in the decoded document text.
		std::size_t logicalRecord = 0; // Zero-based.
		std::size_t physicalLine = 1; // One-based.
		std::size_t column = 1; // One-based wchar_t column.
	};

	struct ParseOptions {
		wchar_t delimiter = L',';
		bool allowWhitespaceAfterQuote = true;
		bool autoDetectDelimiter = false;
	};

	struct ParseResult {
		std::vector<Record> records;
		std::vector<Field> fields; // Flat storage avoids one heap allocation per logical record.
		std::vector<ParseError> errors;
		std::size_t maxFieldCount = 0;
		bool cancelled = false;

		const Field* FieldAt(const Record& record, std::size_t index) const noexcept
		{
			return index < record.fieldCount && record.firstField <= fields.size() &&
				record.fieldCount <= fields.size() - record.firstField
				? &fields[record.firstField + index]
				: nullptr;
		}
	};

	// Ranges refer to the original decoded text; parsing never normalizes or rewrites it.
	ParseResult Parse(std::wstring_view text, ParseOptions options = {}, const std::function<bool()>& isCancelled = {});
	std::wstring DecodeField(std::wstring_view text, const Field& field);
	std::optional<std::wstring> SerializeField(std::wstring_view value, wchar_t delimiter);
	wchar_t DetectDelimiter(std::wstring_view text, std::wstring_view candidates = L",\t;|");
}

#endif // SAKURA_CSVPARSER_H_
