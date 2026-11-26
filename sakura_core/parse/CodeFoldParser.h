/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CODEFOLDPARSER_H_
#define SAKURA_CODEFOLDPARSER_H_
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace codefold {
	enum class Language {
		Curl,
		CLike,
		Xml,
		Sql,
	};

	struct Region {
		size_t firstLine = 0;
		size_t lastLine = 0; // inclusive; the first line remains visible when collapsed
		size_t depth = 0;
		std::wstring kind;
	};

	// Scans already-decoded logical lines; it never joins or edits the source text.
	std::vector<Region> Parse(const std::vector<std::wstring_view>& lines, Language language);
}

#endif // SAKURA_CODEFOLDPARSER_H_
