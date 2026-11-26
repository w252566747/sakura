#include "StdAfx.h"

/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "CSVStatistics.h"

#include <cmath>
#include <cwchar>

namespace csvtable {
namespace {
std::optional<long double> ParseNumber(std::wstring_view text)
{
	while (!text.empty() && (text.front() == L' ' || text.front() == L'\t' || text.front() == L'\r' || text.front() == L'\n')) text.remove_prefix(1);
	while (!text.empty() && (text.back() == L' ' || text.back() == L'\t' || text.back() == L'\r' || text.back() == L'\n')) text.remove_suffix(1);
	if (text.empty()) return std::nullopt;
	std::size_t position = 0;
	if (text[position] == L'+' || text[position] == L'-') ++position;
	if (position == text.size()) return std::nullopt;
	const std::size_t integerBegin = position;
	std::size_t integerDigits = 0;
	std::size_t fractionalDigits = 0;
	std::size_t significantDigits = 0;
	while (position < text.size() && text[position] >= L'0' && text[position] <= L'9') {
		if (text[position] != L'0' || significantDigits != 0) ++significantDigits;
		++integerDigits;
		++position;
	}
	// Leading-zero integers are commonly identifiers (for example 000123), not quantities.
	if (integerDigits > 1 && text[integerBegin] == L'0') return std::nullopt;
	if (position < text.size() && text[position] == L'.') {
		++position;
		while (position < text.size() && text[position] >= L'0' && text[position] <= L'9') {
			if (text[position] != L'0' || significantDigits != 0) ++significantDigits;
			++fractionalDigits;
			++position;
		}
	}
	if (integerDigits == 0 && fractionalDigits == 0) return std::nullopt;
	if (significantDigits > 15) return std::nullopt;
	if (position < text.size() && (text[position] == L'e' || text[position] == L'E')) {
		++position;
		if (position < text.size() && (text[position] == L'+' || text[position] == L'-')) ++position;
		const std::size_t exponentBegin = position;
		while (position < text.size() && text[position] >= L'0' && text[position] <= L'9') ++position;
		if (position == exponentBegin) return std::nullopt;
	}
	if (position != text.size()) return std::nullopt;
	const std::wstring buffer(text);
	wchar_t* end = nullptr;
	const long double value = std::wcstold(buffer.c_str(), &end);
	if (end == buffer.c_str() || !std::isfinite(value)) return std::nullopt;
	return end == buffer.c_str() + buffer.size() ? std::optional<long double>(value) : std::nullopt;
}
}

StatisticsResult CalculateStatistics(
	const CSVTableModel& model,
	const std::vector<CellPosition>& selectedCells)
{
	StatisticsResult result;
	long double sum = 0;
	for (const CellPosition& position : selectedCells) {
		if (position.row >= model.RowCount() || position.column >= model.ColumnCount()) continue;
		++result.selectedCount;
		const auto number = ParseNumber(model.CellText(position.row, position.column));
		if (!number) continue;
		if (!result.minimum || *number < *result.minimum) result.minimum = *number;
		if (!result.maximum || *number > *result.maximum) result.maximum = *number;
		sum += *number;
		++result.numericCount;
	}
	if (result.numericCount != 0) {
		result.sum = sum;
		result.average = sum / static_cast<long double>(result.numericCount);
	}
	return result;
}
}
