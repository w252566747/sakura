/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CSVSTATISTICS_H_
#define SAKURA_CSVSTATISTICS_H_
#pragma once

#include "CSVTableModel.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace csvtable {
	struct CellPosition {
		std::size_t row = 0;
		std::size_t column = 0;
	};

	struct StatisticsResult {
		std::size_t selectedCount = 0;
		std::size_t numericCount = 0;
		std::optional<long double> sum;
		std::optional<long double> average;
		std::optional<long double> maximum;
		std::optional<long double> minimum;
	};

	// Counts each selected cell once; non-numeric cells remain excluded from numeric aggregates.
	StatisticsResult CalculateStatistics(
		const CSVTableModel& model,
		const std::vector<CellPosition>& selectedCells);
}

#endif // SAKURA_CSVSTATISTICS_H_
