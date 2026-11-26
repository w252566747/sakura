#include "StdAfx.h"

/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "CSVParser.h"

#include <algorithm>
#include <map>

namespace csvtable {
namespace {
enum class State { FieldStart, Unquoted, Quoted, AfterQuote };

std::size_t NewlineLength(std::wstring_view text, std::size_t offset)
{
	if (offset >= text.size()) return 0;
	if (text[offset] == L'\r') return offset + 1 < text.size() && text[offset + 1] == L'\n' ? 2 : 1;
	return text[offset] == L'\n' ? 1 : 0;
}
}

ParseResult Parse(std::wstring_view text, ParseOptions options, const std::function<bool()>& isCancelled)
{
	ParseResult result;
	if (options.delimiter == L'"' || options.delimiter == L'\r' || options.delimiter == L'\n' || options.delimiter == L'\0') {
		result.errors.push_back({ ErrorCode::InvalidDelimiter, 0, 0, 1, 1 });
		return result;
	}

	std::size_t recordBegin = 0;
	std::size_t fieldBegin = 0;
	std::size_t valueBegin = 0;
	std::size_t valueEnd = 0;
	std::size_t physicalLine = 1;
	std::size_t recordFirstLine = 1;
	std::size_t recordPhysicalLines = 1;
	std::size_t logicalRecord = 0;
	std::size_t column = 1;
	State state = State::FieldStart;
	bool fieldQuoted = false;
	bool fieldMalformed = false;
	bool recordMalformed = false;
	bool recordHasContent = false;
	std::size_t quoteOffset = 0;
	std::size_t quoteLine = 1;
	std::size_t quoteColumn = 1;

	const auto finishField = [&](std::size_t rawEnd, Record& record) {
		Field field;
		field.rawBegin = fieldBegin;
		field.rawLength = rawEnd - fieldBegin;
		field.valueBegin = valueBegin;
		field.valueLength = valueEnd >= valueBegin ? valueEnd - valueBegin : 0;
		field.quoted = fieldQuoted;
		field.malformed = fieldMalformed;
		result.fields.push_back(field);
		++record.fieldCount;
		record.malformed = record.malformed || fieldMalformed;
	};

	const auto finishRecord = [&](std::size_t rawEnd, std::size_t rawAfterEnd, Record& record) {
		finishField(rawEnd, record);
		record.rawBegin = recordBegin;
		record.rawLength = rawAfterEnd - recordBegin;
		record.firstPhysicalLine = recordFirstLine;
		record.physicalLineCount = recordPhysicalLines;
		record.malformed = record.malformed || recordMalformed;
		result.maxFieldCount = std::max(result.maxFieldCount, record.fieldCount);
		result.records.push_back(std::move(record));
		++logicalRecord;
	};

	Record record;
	record.firstField = 0;
	for (std::size_t i = 0; i < text.size();) {
		if ((i & 0x3fffu) == 0 && isCancelled && isCancelled()) {
			result.cancelled = true;
			return result;
		}
		const std::size_t newlineLength = NewlineLength(text, i);
		const wchar_t ch = text[i];

		if (state == State::Quoted) {
			if (ch == L'"') {
				if (i + 1 < text.size() && text[i + 1] == L'"') {
					i += 2;
					column += 2;
					continue;
				}
				valueEnd = i;
				state = State::AfterQuote;
				++i;
				++column;
				continue;
			}
			if (newlineLength != 0) {
				i += newlineLength;
				++physicalLine;
				++recordPhysicalLines;
				column = 1;
				continue;
			}
			++i;
			++column;
			continue;
		}

		if (state == State::AfterQuote) {
			if (ch == options.delimiter) {
				finishField(i, record);
				++i;
				++column;
				fieldBegin = valueBegin = valueEnd = i;
				fieldQuoted = fieldMalformed = false;
				state = State::FieldStart;
				recordHasContent = true;
				continue;
			}
			if (newlineLength != 0) {
				const std::size_t next = i + newlineLength;
				finishRecord(i, next, record);
				i = next;
				++physicalLine;
				column = 1;
				recordBegin = fieldBegin = valueBegin = valueEnd = i;
				recordFirstLine = physicalLine;
				recordPhysicalLines = 1;
				record = Record{};
				record.firstField = result.fields.size();
				recordMalformed = recordHasContent = false;
				fieldQuoted = fieldMalformed = false;
				state = State::FieldStart;
				continue;
			}
			if (options.allowWhitespaceAfterQuote && (ch == L' ' || ch == L'\t')) {
				++i;
				++column;
				continue;
			}
			result.errors.push_back({ ErrorCode::UnexpectedCharacterAfterQuote, i, logicalRecord, physicalLine, column });
			fieldMalformed = recordMalformed = true;
			state = State::Unquoted;
			++i;
			++column;
			continue;
		}

		if (state == State::FieldStart && ch == L'"') {
			fieldQuoted = true;
			quoteOffset = i;
			quoteLine = physicalLine;
			quoteColumn = column;
			valueBegin = i + 1;
			valueEnd = valueBegin;
			state = State::Quoted;
			recordHasContent = true;
			++i;
			++column;
			continue;
		}
		if (ch == options.delimiter) {
			finishField(i, record);
			++i;
			++column;
			fieldBegin = valueBegin = valueEnd = i;
			fieldQuoted = fieldMalformed = false;
			state = State::FieldStart;
			recordHasContent = true;
			continue;
		}
		if (newlineLength != 0) {
			const std::size_t next = i + newlineLength;
			finishRecord(i, next, record);
			i = next;
			++physicalLine;
			column = 1;
			recordBegin = fieldBegin = valueBegin = valueEnd = i;
			recordFirstLine = physicalLine;
			recordPhysicalLines = 1;
			record = Record{};
			record.firstField = result.fields.size();
			recordMalformed = recordHasContent = false;
			fieldQuoted = fieldMalformed = false;
			state = State::FieldStart;
			continue;
		}
		if (ch == L'"') {
			result.errors.push_back({ ErrorCode::UnexpectedQuote, i, logicalRecord, physicalLine, column });
			fieldMalformed = recordMalformed = true;
		}
		state = State::Unquoted;
		recordHasContent = true;
		++i;
		++column;
		valueEnd = i;
	}

	if (state == State::Quoted) {
		result.errors.push_back({ ErrorCode::UnclosedQuote, quoteOffset, logicalRecord, quoteLine, quoteColumn });
		fieldMalformed = recordMalformed = true;
	}
	if (recordHasContent || recordBegin < text.size()) {
		finishRecord(text.size(), text.size(), record);
	}
	return result;
}

std::wstring DecodeField(std::wstring_view text, const Field& field)
{
	if (field.rawBegin > text.size() || field.rawLength > text.size() - field.rawBegin) return {};
	if (field.malformed) return std::wstring(text.substr(field.rawBegin, field.rawLength));
	if (!field.quoted) {
		if (field.valueBegin > text.size() || field.valueLength > text.size() - field.valueBegin) return {};
		return std::wstring(text.substr(field.valueBegin, field.valueLength));
	}
	if (field.valueBegin > text.size() || field.valueLength > text.size() - field.valueBegin) return {};
	const std::wstring_view value = text.substr(field.valueBegin, field.valueLength);
	std::wstring decoded;
	decoded.reserve(value.size());
	for (std::size_t i = 0; i < value.size(); ++i) {
		decoded.push_back(value[i]);
		if (value[i] == L'"' && i + 1 < value.size() && value[i + 1] == L'"') ++i;
	}
	return decoded;
}

std::optional<std::wstring> SerializeField(std::wstring_view value, wchar_t delimiter)
{
	if (delimiter == L'"' || delimiter == L'\r' || delimiter == L'\n' || delimiter == L'\0') return std::nullopt;
	const bool quote = value.find(delimiter) != std::wstring_view::npos ||
		value.find(L'"') != std::wstring_view::npos ||
		value.find(L'\r') != std::wstring_view::npos ||
		value.find(L'\n') != std::wstring_view::npos ||
		(!value.empty() && (value.front() == L' ' || value.front() == L'\t' || value.back() == L' ' || value.back() == L'\t'));
	if (!quote) return std::wstring(value);
	std::wstring serialized;
	serialized.reserve(value.size() + 2);
	serialized.push_back(L'"');
	for (const wchar_t ch : value) {
		serialized.push_back(ch);
		if (ch == L'"') serialized.push_back(L'"');
	}
	serialized.push_back(L'"');
	return serialized;
}

wchar_t DetectDelimiter(std::wstring_view text, std::wstring_view candidates)
{
	if (candidates.empty()) return L',';
	const std::wstring_view sample = text.substr(0, std::min<std::size_t>(text.size(), 64 * 1024));
	wchar_t best = candidates.front();
	std::size_t bestScore = 0;
	for (wchar_t candidate : candidates) {
		const ParseResult parsed = Parse(sample, ParseOptions{ candidate, true });
		std::map<std::size_t, std::size_t> frequency;
		std::size_t delimitedRecords = 0;
		for (const Record& record : parsed.records) {
			if (record.fieldCount > 1) {
				++frequency[record.fieldCount];
				++delimitedRecords;
			}
		}
		std::size_t modalRecords = 0;
		for (const auto& item : frequency) modalRecords = std::max(modalRecords, item.second);
		const std::size_t score = modalRecords * 1000 + delimitedRecords;
		if (score > bestScore) {
			bestScore = score;
			best = candidate;
		}
	}
	return best;
}
}
