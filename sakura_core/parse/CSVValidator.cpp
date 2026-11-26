/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "CSVValidator.h"

#include <algorithm>
#include <cstdint>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace csvtable {
namespace {
bool IsBlank(std::wstring_view value)
{
	return std::all_of(value.begin(), value.end(), [](wchar_t ch) {
		return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
	});
}

std::wstring ColumnName(std::size_t column, const std::vector<std::wstring>& headers)
{
	return column < headers.size() && !headers[column].empty()
		? headers[column]
		: L"Column " + std::to_wstring(column + 1);
}

std::size_t FieldAtOffset(const ParseResult& parsed, const Record& record, std::size_t offset)
{
	for (std::size_t column = 0; column < record.fieldCount; ++column) {
		const Field* field = parsed.FieldAt(record, column);
		if (field && offset >= field->rawBegin && offset <= field->rawBegin + field->rawLength) return column;
	}
	return 0;
}

struct CompositeKeyHash {
	std::uint64_t first = 14695981039346656037ull;
	std::uint64_t second = 0x9e3779b97f4a7c15ull;
};

void Mix(CompositeKeyHash& hash, std::uint32_t value)
{
	for (unsigned shift = 0; shift < 32; shift += 8) {
		const std::uint64_t byte = (value >> shift) & 0xffu;
		hash.first = (hash.first ^ byte) * 1099511628211ull;
		hash.second ^= byte + 0x9e3779b97f4a7c15ull + (hash.second << 6) + (hash.second >> 2);
	}
}

bool MakeCompositeKeyHash(
	std::wstring_view text,
	const ParseResult& parsed,
	const Record& record,
	const std::vector<std::size_t>& columns,
	CompositeKeyHash* result)
{
	CompositeKeyHash hash;
	for (const std::size_t column : columns) {
		const Field* field = parsed.FieldAt(record, column);
		if (!field) return false;
		const std::wstring value = DecodeField(text, *field);
		if (IsBlank(value)) return false;
		Mix(hash, static_cast<std::uint32_t>(value.size()));
		for (const wchar_t ch : value) Mix(hash, static_cast<std::uint32_t>(ch));
		Mix(hash, 0xffffffffu); // Component boundary.
	}
	*result = hash;
	return true;
}

bool CompositeKeysEqual(
	std::wstring_view text,
	const ParseResult& parsed,
	const Record& lhs,
	const Record& rhs,
	const std::vector<std::size_t>& columns)
{
	for (const std::size_t column : columns) {
		const Field* lhsField = parsed.FieldAt(lhs, column);
		const Field* rhsField = parsed.FieldAt(rhs, column);
		if (!lhsField || !rhsField || DecodeField(text, *lhsField) != DecodeField(text, *rhsField)) return false;
	}
	return true;
}
}

std::vector<Diagnostic> Validate(
	std::wstring_view text,
	const ParseResult& parsed,
	const ValidationOptions& options,
	const std::function<bool()>& isCancelled)
{
	std::vector<Diagnostic> diagnostics;
	std::vector<std::wstring> headers;
	headers.reserve(parsed.records.empty() ? 0 : parsed.records.front().fieldCount);
	if (options.hasHeader && !parsed.records.empty()) {
		const Record& header = parsed.records.front();
		std::unordered_map<std::wstring, std::size_t> seenHeaders;
		for (std::size_t column = 0; column < header.fieldCount; ++column) {
			if ((column & 0x3ffu) == 0 && isCancelled && isCancelled()) return {};
			const Field* field = parsed.FieldAt(header, column);
			const std::wstring name = field ? DecodeField(text, *field) : std::wstring{};
			headers.push_back(name);
			if (IsBlank(name)) {
				diagnostics.push_back({ Severity::Error, L"CSV011", L"ヘッダー名が空欄です。", 1,
					header.firstPhysicalLine, column + 1, ColumnName(column, headers), field ? field->rawBegin : header.rawBegin });
			} else {
				const auto insertion = seenHeaders.emplace(name, column);
				if (!insertion.second) {
					diagnostics.push_back({ Severity::Error, L"CSV012", L"ヘッダー名が重複しています。", 1,
						header.firstPhysicalLine, column + 1, name, field ? field->rawBegin : header.rawBegin });
				}
			}
		}
	}

	for (std::size_t errorIndex = 0; errorIndex < parsed.errors.size(); ++errorIndex) {
		if ((errorIndex & 0x3ffu) == 0 && isCancelled && isCancelled()) return {};
		const ParseError& error = parsed.errors[errorIndex];
		const Record* record = error.logicalRecord < parsed.records.size() ? &parsed.records[error.logicalRecord] : nullptr;
		const std::size_t column = record ? FieldAtOffset(parsed, *record, error.offset) : 0;
		const Field* field = record ? parsed.FieldAt(*record, column) : nullptr;
		const wchar_t* code = L"CSV001";
		const wchar_t* message = L"ダブルクォーテーションの位置が正しくありません。";
		switch (error.code) {
		case ErrorCode::InvalidDelimiter:
			code = L"CSV000";
			message = L"区切り文字が正しくありません。";
			break;
		case ErrorCode::UnexpectedCharacterAfterQuote:
			code = L"CSV002";
			message = L"ダブルクォーテーションの後に不正な文字があります。";
			break;
		case ErrorCode::UnclosedQuote:
			code = L"CSV003";
			message = L"ダブルクォーテーションが閉じられていません。";
			break;
		case ErrorCode::UnexpectedQuote:
		default:
			break;
		}
		diagnostics.push_back({ Severity::Error, code, message, error.logicalRecord + 1, error.physicalLine,
			column + 1, ColumnName(column, headers), field ? field->rawBegin : error.offset });
	}

	if (parsed.records.empty()) return diagnostics;
	const std::size_t firstDataRecord = options.hasHeader ? 1 : 0;
	const std::size_t expectedColumns = parsed.records[firstDataRecord < parsed.records.size() ? firstDataRecord : 0].fieldCount;
	const Severity countSeverity = options.inconsistentFieldCountIsWarning ? Severity::Warning : Severity::Error;
	struct KeyRef { CompositeKeyHash hash; std::size_t recordIndex; };
	std::vector<KeyRef> keyRefs;
	if (!options.primaryKeyColumns.empty()) keyRefs.reserve(parsed.records.size() - firstDataRecord);
	for (std::size_t row = firstDataRecord; row < parsed.records.size(); ++row) {
		if ((row & 0x3ffu) == 0 && isCancelled && isCancelled()) return {};
		const Record& record = parsed.records[row];
		if (record.fieldCount != expectedColumns) {
			const std::size_t column = std::min(record.fieldCount, expectedColumns);
			const Field* field = parsed.FieldAt(record, column);
			diagnostics.push_back({ countSeverity, L"CSV010", L"列数が一致していません。", row + 1,
				record.firstPhysicalLine, column + 1, ColumnName(column, headers), field ? field->rawBegin : record.rawBegin + record.rawLength });
		}

		for (const std::size_t column : options.requiredColumns) {
			const Field* field = parsed.FieldAt(record, column);
			if (!field || IsBlank(DecodeField(text, *field))) {
				diagnostics.push_back({ Severity::Error, L"CSV013", L"必須項目が空欄です。", row + 1,
					record.firstPhysicalLine, column + 1, ColumnName(column, headers), field ? field->rawBegin : record.rawBegin + record.rawLength });
			}
		}

		if (!options.primaryKeyColumns.empty()) {
			CompositeKeyHash hash;
			if (MakeCompositeKeyHash(text, parsed, record, options.primaryKeyColumns, &hash)) keyRefs.push_back({ hash, row });
		}
	}
	if (!keyRefs.empty()) {
		std::sort(keyRefs.begin(), keyRefs.end(), [](const KeyRef& lhs, const KeyRef& rhs) {
			return std::tie(lhs.hash.first, lhs.hash.second, lhs.recordIndex) <
				std::tie(rhs.hash.first, rhs.hash.second, rhs.recordIndex);
		});
		for (std::size_t begin = 0; begin < keyRefs.size();) {
			if ((begin & 0x3ffu) == 0 && isCancelled && isCancelled()) return {};
			std::size_t end = begin + 1;
			while (end < keyRefs.size() && keyRefs[end].hash.first == keyRefs[begin].hash.first &&
				keyRefs[end].hash.second == keyRefs[begin].hash.second) ++end;
			std::vector<std::size_t> uniqueRows;
			for (std::size_t i = begin; i < end; ++i) {
				const std::size_t row = keyRefs[i].recordIndex;
				const auto duplicate = std::find_if(uniqueRows.begin(), uniqueRows.end(), [&](std::size_t previousRow) {
					return CompositeKeysEqual(text, parsed, parsed.records[previousRow], parsed.records[row], options.primaryKeyColumns);
				});
				if (duplicate == uniqueRows.end()) {
					uniqueRows.push_back(row);
					continue;
				}
				const Record& record = parsed.records[row];
				const std::size_t column = options.primaryKeyColumns.front();
				const Field* field = parsed.FieldAt(record, column);
				diagnostics.push_back({ Severity::Error, L"CSV014", L"キー項目が重複しています。", row + 1,
					record.firstPhysicalLine, column + 1, ColumnName(column, headers), field ? field->rawBegin : record.rawBegin });
			}
			begin = end;
		}
	}
	std::stable_sort(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& lhs, const Diagnostic& rhs) {
		return std::tie(lhs.record, lhs.column, lhs.textOffset, lhs.code) <
			std::tie(rhs.record, rhs.column, rhs.textOffset, rhs.code);
	});
	return diagnostics;
}
}
