/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CSVVALIDATOR_H_
#define SAKURA_CSVVALIDATOR_H_
#pragma once

#include "CSVParser.h"

#include <string>
#include <functional>
#include <vector>

namespace csvtable {
	enum class Severity { Warning, Error };

	struct ValidationOptions {
		bool hasHeader = false;
		bool inconsistentFieldCountIsWarning = false;
		std::vector<std::size_t> requiredColumns; // Zero-based field indexes.
		std::vector<std::size_t> primaryKeyColumns; // Zero-based; multiple columns form a composite key.
	};

	struct Diagnostic {
		Severity severity = Severity::Error;
		std::wstring code;
		std::wstring message;
		std::size_t record = 0; // One-based logical CSV record.
		std::size_t physicalLine = 0; // One-based source line.
		std::size_t column = 0; // One-based CSV field number.
		std::wstring columnName;
		std::size_t textOffset = 0; // Offset into the decoded original document.
	};

	std::vector<Diagnostic> Validate(
		std::wstring_view text,
		const ParseResult& parsed,
		const ValidationOptions& options = {},
		const std::function<bool()>& isCancelled = {});
}

#endif // SAKURA_CSVVALIDATOR_H_
