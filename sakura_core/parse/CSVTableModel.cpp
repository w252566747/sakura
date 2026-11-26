/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "CSVTableModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <optional>
#include <regex>
#include <tuple>
#include <utility>

namespace csvtable {
CSVTableSnapshot::CSVTableSnapshot(std::wstring text, ParseOptions options, std::uint64_t documentVersion,
	const std::function<bool()>& isCancelled)
	: m_text(std::move(text))
	, m_options(options)
	, m_documentVersion(documentVersion)
{
	if (m_options.autoDetectDelimiter) m_options.delimiter = DetectDelimiter(m_text);
	m_parsed = Parse(m_text, m_options, isCancelled);
}

std::shared_ptr<const CSVTableSnapshot> CSVTableSnapshot::Create(
	std::wstring text,
	ParseOptions options,
	std::uint64_t documentVersion,
	const std::function<bool()>& isCancelled)
{
	auto snapshot = std::shared_ptr<const CSVTableSnapshot>(
		new CSVTableSnapshot(std::move(text), options, documentVersion, isCancelled));
	return snapshot->Parsed().cancelled ? nullptr : snapshot;
}

CSVTableModel::CSVTableModel(std::shared_ptr<const CSVTableSnapshot> snapshot, bool hasHeader)
	: m_snapshot(std::move(snapshot))
	, m_hasHeader(hasHeader)
{
}

std::size_t CSVTableModel::RowCount() const noexcept
{
	if (!m_snapshot) return 0;
	const std::size_t records = m_snapshot->Parsed().records.size();
	return m_hasHeader && records != 0 ? records - 1 : records;
}

std::size_t CSVTableModel::ColumnCount() const noexcept
{
	return m_snapshot ? m_snapshot->Parsed().maxFieldCount : 0;
}

std::uint64_t CSVTableModel::DocumentVersion() const noexcept
{
	return m_snapshot ? m_snapshot->DocumentVersion() : 0;
}

std::size_t CSVTableModel::RecordIndexForRow(std::size_t dataRow) const noexcept
{
	if (!m_snapshot || dataRow >= RowCount()) return m_snapshot ? m_snapshot->Parsed().records.size() : 0;
	return dataRow + (m_hasHeader ? 1 : 0);
}

const Record* CSVTableModel::RecordAtDataRow(std::size_t dataRow) const noexcept
{
	if (!m_snapshot) return nullptr;
	const std::size_t index = RecordIndexForRow(dataRow);
	const auto& records = m_snapshot->Parsed().records;
	return index < records.size() ? &records[index] : nullptr;
}

std::wstring CSVTableModel::ColumnTitle(std::size_t column) const
{
	if (m_hasHeader && m_snapshot && !m_snapshot->Parsed().records.empty()) {
		const Record& header = m_snapshot->Parsed().records.front();
		if (const Field* field = m_snapshot->Parsed().FieldAt(header, column)) {
			return DecodeField(m_snapshot->Text(), *field);
		}
	}
	return L"Column " + std::to_wstring(column + 1);
}

std::wstring CSVTableModel::CellText(std::size_t dataRow, std::size_t column) const
{
	const Record* record = RecordAtDataRow(dataRow);
	if (!record || !m_snapshot) return {};
	const Field* field = m_snapshot->Parsed().FieldAt(*record, column);
	return field ? DecodeField(m_snapshot->Text(), *field) : std::wstring{};
}

RawFieldRange CSVTableModel::CellRawRange(std::size_t dataRow, std::size_t column) const noexcept
{
	const Record* record = RecordAtDataRow(dataRow);
	if (!record || !m_snapshot) return {};
	const Field* field = m_snapshot->Parsed().FieldAt(*record, column);
	return field ? RawFieldRange{ field->rawBegin, field->rawLength, true } : RawFieldRange{};
}

TextEditPatch CSVTableModel::PrepareCellEdit(std::size_t dataRow, std::size_t column, std::wstring_view value) const
{
	const RawFieldRange range = CellRawRange(dataRow, column);
	if (!range.valid || !m_snapshot) return {};
	const auto serialized = SerializeField(value, m_snapshot->Options().delimiter);
	if (!serialized) return {};
	return { range.begin, range.length, *serialized, true, DocumentVersion() };
}

TextEditPatch CSVTableModel::PrepareAppendRow(const std::vector<std::wstring>& values) const
{
	if (!m_snapshot) return {};
	if (std::any_of(m_snapshot->Parsed().errors.begin(), m_snapshot->Parsed().errors.end(), [](const ParseError& error) {
		return error.code == ErrorCode::UnclosedQuote;
	})) return {};
	const std::wstring_view text = m_snapshot->Text();
	const wchar_t delimiter = m_snapshot->Options().delimiter;
	std::wstring row;
	const std::size_t fieldCount = values.empty() ? std::max<std::size_t>(ColumnCount(), 1) : values.size();
	for (std::size_t column = 0; column < fieldCount; ++column) {
		if (column != 0) row.push_back(delimiter);
		const std::wstring_view value = values.empty() ? std::wstring_view{} : std::wstring_view(values[column]);
		const auto serialized = SerializeField(value, delimiter);
		if (!serialized) return {};
		row.append(*serialized);
	}
	std::wstring lineEnding = L"\r\n";
	for (std::size_t i = 0; i < text.size(); ++i) {
		if (text[i] == L'\r') {
			lineEnding = i + 1 < text.size() && text[i + 1] == L'\n' ? L"\r\n" : L"\r";
			break;
		}
		if (text[i] == L'\n') {
			lineEnding = L"\n";
			break;
		}
	}
	if (!text.empty() && text.back() != L'\r' && text.back() != L'\n') row.insert(0, lineEnding);
	if (row.empty()) row = lineEnding;
	return { text.size(), 0, std::move(row), true, DocumentVersion() };
}

TextEditPatch CSVTableModel::PrepareDeleteRow(std::size_t dataRow) const noexcept
{
	const Record* record = RecordAtDataRow(dataRow);
	return record ? TextEditPatch{ record->rawBegin, record->rawLength, {}, true, DocumentVersion() } : TextEditPatch{};
}

TextEditBatch CSVTableModel::PrepareAppendColumn() const
{
	TextEditBatch patches;
	if (!m_snapshot) return patches;
	const auto& records = m_snapshot->Parsed().records;
	patches.reserve(records.size());
	const std::wstring_view text = m_snapshot->Text();
	for (const Record& record : records) {
		if (record.malformed || record.rawBegin > text.size() || record.rawLength > text.size() - record.rawBegin) return {};
		std::size_t end = record.rawBegin + record.rawLength;
		if (end > record.rawBegin && text[end - 1] == L'\n') --end;
		if (end > record.rawBegin && text[end - 1] == L'\r') --end;
		patches.push_back({ end, 0, std::wstring(1, m_snapshot->Options().delimiter), true, DocumentVersion() });
	}
	std::sort(patches.begin(), patches.end(), [](const TextEditPatch& lhs, const TextEditPatch& rhs) {
		return lhs.begin > rhs.begin;
	});
	return patches;
}

TextEditBatch CSVTableModel::PrepareDeleteColumn(std::size_t column) const
{
	TextEditBatch patches;
	if (!m_snapshot || column >= ColumnCount()) return patches;
	const auto& parsed = m_snapshot->Parsed();
	const std::wstring_view text = m_snapshot->Text();
	patches.reserve(parsed.records.size());
	for (const Record& record : parsed.records) {
		if (column >= record.fieldCount) continue;
		if (record.malformed) return {};
		const Field* current = parsed.FieldAt(record, column);
		if (!current) return {};
		std::size_t begin = current->rawBegin;
		std::size_t end = current->rawBegin + current->rawLength;
		if (record.fieldCount > 1 && column == 0) {
			const Field* next = parsed.FieldAt(record, 1);
			if (!next) return {};
			end = next->rawBegin;
		} else if (column > 0) {
			const Field* previous = parsed.FieldAt(record, column - 1);
			if (!previous) return {};
			begin = previous->rawBegin + previous->rawLength;
		}
		if (begin > text.size() || end > text.size() || begin > end) return {};
		patches.push_back({ begin, end - begin, {}, true, DocumentVersion() });
	}
	std::sort(patches.begin(), patches.end(), [](const TextEditPatch& lhs, const TextEditPatch& rhs) {
		return lhs.begin > rhs.begin;
	});
	return patches;
}

std::size_t CSVTableModel::FirstPhysicalLine(std::size_t dataRow) const noexcept
{
	const Record* record = RecordAtDataRow(dataRow);
	return record ? record->firstPhysicalLine : 0;
}

namespace {
bool IsBlank(std::wstring_view value)
{
	return std::all_of(value.begin(), value.end(), [](wchar_t ch) {
		return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
	});
}

wchar_t Fold(wchar_t ch, bool caseSensitive)
{
	return caseSensitive ? ch : static_cast<wchar_t>(std::towlower(ch));
}

bool EqualText(std::wstring_view lhs, std::wstring_view rhs, bool caseSensitive)
{
	if (lhs.size() != rhs.size()) return false;
	for (std::size_t i = 0; i < lhs.size(); ++i) {
		if (Fold(lhs[i], caseSensitive) != Fold(rhs[i], caseSensitive)) return false;
	}
	return true;
}

bool ContainsText(std::wstring_view text, std::wstring_view value, bool caseSensitive)
{
	if (value.empty()) return true;
	if (value.size() > text.size()) return false;
	for (std::size_t start = 0; start <= text.size() - value.size(); ++start) {
		if (EqualText(text.substr(start, value.size()), value, caseSensitive)) return true;
	}
	return false;
}

std::optional<double> ParseNumber(std::wstring_view value)
{
	if (value.empty()) return std::nullopt;
	const std::wstring buffer(value);
	wchar_t* end = nullptr;
	const double number = std::wcstod(buffer.c_str(), &end);
	if (end == buffer.c_str() || !std::isfinite(number)) return std::nullopt;
	while (*end == L' ' || *end == L'\t' || *end == L'\r' || *end == L'\n') ++end;
	return *end == L'\0' ? std::optional<double>(number) : std::nullopt;
}

std::optional<std::array<int, 3>> ParseDate(std::wstring_view value)
{
	std::array<int, 3> date{};
	if (value.size() == 8) {
		for (std::size_t i = 0; i < 8; ++i) {
			if (value[i] < L'0' || value[i] > L'9') return std::nullopt;
		}
		for (std::size_t i = 0; i < 4; ++i) date[0] = date[0] * 10 + value[i] - L'0';
		date[1] = (value[4] - L'0') * 10 + value[5] - L'0';
		date[2] = (value[6] - L'0') * 10 + value[7] - L'0';
	} else if (value.size() == 10 && (value[4] == L'-' || value[4] == L'/') && value[7] == value[4]) {
		const int positions[] = { 0, 1, 2, 3, 5, 6, 8, 9 };
		for (const int pos : positions) {
			if (value[static_cast<std::size_t>(pos)] < L'0' || value[static_cast<std::size_t>(pos)] > L'9') return std::nullopt;
		}
		for (std::size_t i = 0; i < 4; ++i) date[0] = date[0] * 10 + value[i] - L'0';
		date[1] = (value[5] - L'0') * 10 + value[6] - L'0';
		date[2] = (value[8] - L'0') * 10 + value[9] - L'0';
	} else {
		return std::nullopt;
	}
	const int year = date[0];
	const int month = date[1];
	const int day = date[2];
	if (year < 1 || month < 1 || month > 12) return std::nullopt;
	const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
	const int monthDays[] = { 31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	return day >= 1 && day <= monthDays[month - 1] ? std::optional<std::array<int, 3>>(date) : std::nullopt;
}

class FieldCursor final {
public:
	FieldCursor() = default;
	FieldCursor(std::wstring_view text, const Field* field)
		: m_text(text)
	{
		if (!field) return;
		if (field->malformed) {
			m_position = field->rawBegin;
			m_end = field->rawBegin + field->rawLength;
		} else {
			m_position = field->valueBegin;
			m_end = field->valueBegin + field->valueLength;
			m_collapseEscapedQuotes = field->quoted;
		}
		m_end = std::min(m_end, m_text.size());
	}

	bool Peek(wchar_t* value) const
	{
		FieldCursor copy = *this;
		return copy.Next(value);
	}

	bool Next(wchar_t* value)
	{
		if (m_position >= m_end) return false;
		*value = m_text[m_position++];
		if (m_collapseEscapedQuotes && *value == L'"' && m_position < m_end && m_text[m_position] == L'"') ++m_position;
		return true;
	}

private:
	std::wstring_view m_text;
	std::size_t m_position = 0;
	std::size_t m_end = 0;
	bool m_collapseEscapedQuotes = false;
};

int CompareFieldText(std::wstring_view text, const Field* lhs, const Field* rhs)
{
	FieldCursor left(text, lhs);
	FieldCursor right(text, rhs);
	for (;;) {
		wchar_t a = 0;
		wchar_t b = 0;
		const bool hasA = left.Next(&a);
		const bool hasB = right.Next(&b);
		if (hasA != hasB) return hasA ? 1 : -1;
		if (!hasA) return 0;
		if (a != b) return a < b ? -1 : 1;
	}
}

int CompareNaturalFields(std::wstring_view text, const Field* lhsField, const Field* rhsField)
{
	FieldCursor lhs(text, lhsField);
	FieldCursor rhs(text, rhsField);
	for (;;) {
		wchar_t a = 0;
		wchar_t b = 0;
		const bool hasA = lhs.Peek(&a);
		const bool hasB = rhs.Peek(&b);
		if (hasA != hasB) return hasA ? 1 : -1;
		if (!hasA) return 0;
		const bool digitsA = a >= L'0' && a <= L'9';
		const bool digitsB = b >= L'0' && b <= L'9';
		if (digitsA && digitsB) {
			struct DigitRun { FieldCursor significant; FieldCursor end; std::size_t significantLength = 0; std::size_t totalLength = 0; };
			const auto readDigits = [](FieldCursor cursor) {
				DigitRun run;
				wchar_t digit = 0;
				while (cursor.Peek(&digit) && digit >= L'0' && digit <= L'9') {
					const FieldCursor digitStart = cursor;
					cursor.Next(&digit);
					++run.totalLength;
					if (run.significantLength == 0 && digit == L'0') {
						run.significant = cursor;
						continue;
					}
					if (run.significantLength == 0) run.significant = digitStart;
					++run.significantLength;
				}
				run.end = cursor;
				return run;
			};
			const DigitRun leftRun = readDigits(lhs);
			const DigitRun rightRun = readDigits(rhs);
			if (leftRun.significantLength != rightRun.significantLength)
				return leftRun.significantLength < rightRun.significantLength ? -1 : 1;
			FieldCursor leftDigits = leftRun.significant;
			FieldCursor rightDigits = rightRun.significant;
			for (std::size_t i = 0; i < leftRun.significantLength; ++i) {
				leftDigits.Next(&a);
				rightDigits.Next(&b);
				if (a != b) return a < b ? -1 : 1;
			}
			if (leftRun.totalLength != rightRun.totalLength)
				return leftRun.totalLength < rightRun.totalLength ? -1 : 1;
			lhs = leftRun.end;
			rhs = rightRun.end;
			continue;
		}
		lhs.Next(&a);
		rhs.Next(&b);
		a = static_cast<wchar_t>(std::towlower(a));
		b = static_cast<wchar_t>(std::towlower(b));
		if (a != b) return a < b ? -1 : 1;
	}
}

bool MatchesFilter(std::wstring_view cell, const Filter& filter,
	const std::optional<double>& filterNumber, const std::optional<std::wregex>& regex)
{
	const std::wstring_view pattern(filter.value);
	switch (filter.op) {
	case FilterOperator::Contains:
		return ContainsText(cell, pattern, filter.caseSensitive);
	case FilterOperator::NotContains:
		return !ContainsText(cell, pattern, filter.caseSensitive);
	case FilterOperator::Equals:
		return EqualText(cell, pattern, filter.caseSensitive);
	case FilterOperator::StartsWith:
		return cell.size() >= pattern.size() && EqualText(cell.substr(0, pattern.size()), pattern, filter.caseSensitive);
	case FilterOperator::EndsWith:
		return cell.size() >= pattern.size() && EqualText(cell.substr(cell.size() - pattern.size()), pattern, filter.caseSensitive);
	case FilterOperator::Regex:
		return regex && std::regex_search(cell.begin(), cell.end(), *regex);
	case FilterOperator::NumberGreater:
	case FilterOperator::NumberLess:
	case FilterOperator::NumberEqual: {
		const auto number = ParseNumber(cell);
		if (!number || !filterNumber) return false;
		if (filter.op == FilterOperator::NumberGreater) return *number > *filterNumber;
		if (filter.op == FilterOperator::NumberLess) return *number < *filterNumber;
		return *number == *filterNumber;
	}
	case FilterOperator::Empty:
		return IsBlank(cell);
	case FilterOperator::NonEmpty:
		return !IsBlank(cell);
	}
	return false;
}

struct SortValue {
	const Field* field = nullptr;
	std::optional<double> number;
	std::optional<std::array<int, 3>> date;
};
}

ViewOrderResult CSVTableModel::BuildViewOrder(
	const std::vector<Filter>& filters,
	FilterLogic logic,
	const std::vector<SortKey>& sortKeys,
	const std::function<bool()>& isCancelled) const
{
	ViewOrderResult result;
	if (filters.empty() && sortKeys.empty()) return result;
	result.identity = false;
	if (!m_snapshot) return result;

	std::vector<std::optional<std::wregex>> regexes(filters.size());
	std::vector<std::optional<double>> filterNumbers(filters.size());
	for (std::size_t i = 0; i < filters.size(); ++i) {
		const Filter& filter = filters[i];
		if (filter.column >= ColumnCount()) {
			result.invalidFilter = i;
			return result;
		}
		if (filter.op == FilterOperator::Regex) {
			try {
				const auto flags = std::regex_constants::ECMAScript |
					(filter.caseSensitive ? std::regex_constants::syntax_option_type{} : std::regex_constants::icase);
				regexes[i].emplace(filter.value, flags);
			} catch (const std::regex_error&) {
				result.invalidFilter = i;
				return result;
			}
		}
		if (filter.op == FilterOperator::NumberGreater || filter.op == FilterOperator::NumberLess ||
			filter.op == FilterOperator::NumberEqual) {
			filterNumbers[i] = ParseNumber(filter.value);
			if (!filterNumbers[i]) {
				result.invalidFilter = i;
				return result;
			}
		}
	}

	const auto cancelled = [&]() { return isCancelled && isCancelled(); };
	for (std::size_t row = 0; row < RowCount(); ++row) {
		if ((row & 0xffu) == 0 && cancelled()) {
			result.rows.clear();
			result.cancelled = true;
			return result;
		}
		bool include = filters.empty() || logic == FilterLogic::And;
		for (std::size_t i = 0; i < filters.size(); ++i) {
			const std::wstring cell = CellText(row, filters[i].column);
			const bool matches = MatchesFilter(cell, filters[i], filterNumbers[i], regexes[i]);
			if (logic == FilterLogic::And) include = include && matches;
			else include = include || matches;
			if ((logic == FilterLogic::And && !include) || (logic == FilterLogic::Or && include)) break;
		}
		if (include) result.rows.push_back(row);
	}

	if (!sortKeys.empty()) {
		std::vector<std::vector<SortValue>> values;
		values.resize(sortKeys.size());
		for (std::size_t keyIndex = 0; keyIndex < sortKeys.size(); ++keyIndex) {
			const SortKey& key = sortKeys[keyIndex];
			values[keyIndex].reserve(result.rows.size());
			for (const std::size_t row : result.rows) {
				SortValue value;
				const Record* record = RecordAtDataRow(row);
				value.field = record ? m_snapshot->Parsed().FieldAt(*record, key.column) : nullptr;
				if (key.kind == SortKind::Numeric) value.number = ParseNumber(CellText(row, key.column));
				if (key.kind == SortKind::Date) value.date = ParseDate(CellText(row, key.column));
				values[keyIndex].push_back(std::move(value));
			}
		}
		std::vector<std::size_t> order(result.rows.size());
		for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
		struct CancelledSort {};
		std::size_t comparisons = 0;
		try {
			std::stable_sort(order.begin(), order.end(), [&](std::size_t lhsIndex, std::size_t rhsIndex) {
				if (((++comparisons) & 0x3ffu) == 0 && cancelled()) throw CancelledSort{};
				for (std::size_t keyIndex = 0; keyIndex < sortKeys.size(); ++keyIndex) {
					const SortKey& key = sortKeys[keyIndex];
					const SortValue& lhs = values[keyIndex][lhsIndex];
					const SortValue& rhs = values[keyIndex][rhsIndex];
					int comparison = 0;
					switch (key.kind) {
					case SortKind::Numeric:
						if (lhs.number.has_value() != rhs.number.has_value()) comparison = lhs.number ? -1 : 1;
						else if (lhs.number && rhs.number) comparison = *lhs.number < *rhs.number ? -1 : (*lhs.number > *rhs.number ? 1 : 0);
						else comparison = CompareFieldText(m_snapshot->Text(), lhs.field, rhs.field);
						break;
					case SortKind::Date:
						if (lhs.date.has_value() != rhs.date.has_value()) comparison = lhs.date ? -1 : 1;
						else if (lhs.date && rhs.date) comparison = *lhs.date < *rhs.date ? -1 : (*lhs.date > *rhs.date ? 1 : 0);
						else comparison = CompareFieldText(m_snapshot->Text(), lhs.field, rhs.field);
						break;
					case SortKind::Natural:
						comparison = CompareNaturalFields(m_snapshot->Text(), lhs.field, rhs.field);
						break;
					case SortKind::String:
					default:
						comparison = CompareFieldText(m_snapshot->Text(), lhs.field, rhs.field);
						break;
					}
					if (comparison != 0) return key.descending ? comparison > 0 : comparison < 0;
				}
				return result.rows[lhsIndex] < result.rows[rhsIndex];
			});
		} catch (const CancelledSort&) {
			result.rows.clear();
			result.cancelled = true;
			return result;
		}
		if (cancelled()) {
			result.rows.clear();
			result.cancelled = true;
			return result;
		}
		std::vector<std::size_t> sorted;
		sorted.reserve(result.rows.size());
		for (const std::size_t index : order) sorted.push_back(result.rows[index]);
		result.rows.swap(sorted);
	}
	return result;
}
}
