/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CURL_RAWREGION_H_
#define SAKURA_CURL_RAWREGION_H_
#pragma once

#include <cwctype>
#include <limits>
#include <string>
#include <string_view>

namespace curlsyntax {

//! Parse Curl's regular, tagged and length-delimited raw comment/string opener.
inline bool TryGetRawRegionTerminator(std::wstring_view line, std::size_t start, wchar_t delimiter,
	std::wstring& terminator, std::size_t& bodyLength, std::size_t& openerLength)
{
	if (start >= line.size() || line[start] != L'|' || start + 1 >= line.size()) return false;
	bodyLength = std::numeric_limits<std::size_t>::max();
	openerLength = 0;
	std::size_t p = start + 1;
	if (line[p] == delimiter) {
		terminator.assign(1, delimiter);
		terminator.push_back(L'|');
		openerLength = 2;
		return true;
	}
	const std::size_t tagStart = p;
	const bool lengthDelimited = line[p] >= L'0' && line[p] <= L'9';
	if (lengthDelimited) {
		while (p < line.size() && line[p] >= L'0' && line[p] <= L'9') ++p;
		std::size_t parsedLength = 0;
		for (std::size_t i = tagStart; i < p; ++i) {
			const std::size_t digit = static_cast<std::size_t>(line[i] - L'0');
			if (parsedLength > (std::numeric_limits<std::size_t>::max() - digit) / 10) return false;
			parsedLength = parsedLength * 10 + digit;
		}
		bodyLength = parsedLength;
	} else if (std::iswalpha(line[p]) || line[p] == L'_') {
		++p;
		while (p < line.size() && (std::iswalnum(line[p]) || line[p] == L'_' || line[p] == L'-' || line[p] == L'?')) ++p;
	} else {
		return false;
	}
	if (p >= line.size() || line[p] != delimiter) return false;
	terminator.assign(1, delimiter);
	if (delimiter == L'"' && !lengthDelimited) terminator.push_back(L'|');
	terminator.append(line.substr(tagStart, p - tagStart));
	terminator.push_back(L'|');
	openerLength = p - start + 1;
	return true;
}

inline bool TryGetVerbatimTerminator(std::wstring_view line, std::size_t start,
	std::wstring& terminator, std::size_t& bodyLength, std::size_t& openerLength)
{
	return TryGetRawRegionTerminator(line, start, L'"', terminator, bodyLength, openerLength);
}

inline bool TryGetCommentTerminator(std::wstring_view line, std::size_t start,
	std::wstring& terminator, std::size_t& bodyLength, std::size_t& openerLength)
{
	return TryGetRawRegionTerminator(line, start, L'#', terminator, bodyLength, openerLength);
}

} // namespace curlsyntax

#endif // SAKURA_CURL_RAWREGION_H_
