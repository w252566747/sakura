/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "curl/CurlRawRegion.h"
#include "parse/CodeFoldParser.h"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <utility>

namespace codefold {
namespace {
struct OpenBlock {
	size_t line;
	size_t depth;
	std::wstring kind;
};

struct CurlTextExpressionContext {
	size_t baseDepth;
	wchar_t quote;
};

bool IsCurlBraceEscaped(std::wstring_view line, size_t position)
{
	if (position >= line.size() || (line[position] != L'{' && line[position] != L'}')) return false;
	size_t backslashes = 0;
	while (position > backslashes && line[position - backslashes - 1] == L'\\') ++backslashes;
	return (backslashes & 1u) != 0;
}

std::wstring TrimmedPrefix(std::wstring_view line, size_t maxLength = 48)
{
	while (!line.empty() && std::iswspace(line.front())) line.remove_prefix(1);
	while (!line.empty() && std::iswspace(line.back())) line.remove_suffix(1);
	if (line.size() > maxLength) line = line.substr(0, maxLength);
	return std::wstring(line);
}

void AddClosedRegion(std::vector<Region>& regions, const OpenBlock& open, size_t endLine)
{
	if (endLine > open.line) regions.push_back({ open.line, endLine, open.depth, open.kind });
}

std::wstring BraceKind(std::wstring_view line, Language language)
{
	const std::wstring prefix = TrimmedPrefix(line);
	if (language == Language::Curl) {
		static const wchar_t* const words[] = {
			L"define-class", L"define-proc", L"class-proc", L"method", L"constructor",
			L"if", L"for", L"while", L"VBox", L"HBox", L"Frame"
		};
		for (const wchar_t* word : words) {
			if (prefix.find(word) != std::wstring::npos) return word;
		}
	}
	return prefix.empty() ? L"{}" : prefix;
}

std::vector<Region> ParseBraces(const std::vector<std::wstring_view>& lines, Language language)
{
	std::vector<Region> regions;
	std::vector<OpenBlock> stack;
	std::vector<CurlTextExpressionContext> curlTextExpressions;
	bool blockComment = false;
	std::wstring rawStringEnd;
	std::size_t curlRawOpenerRemaining = 0;
	bool curlRawLengthSpecified = false;
	std::size_t curlRawRemaining = 0;
	wchar_t continuedQuote = 0;
	bool continuedLineComment = false;

	for (size_t lineNo = 0; lineNo < lines.size(); ++lineNo) {
		const auto line = lines[lineNo];
		wchar_t quote = continuedQuote;
		bool escaped = false;
		bool lineCommentContinues = false;
		if (continuedLineComment) {
			lineCommentContinues = !line.empty() && line.back() == L'\\';
			continuedLineComment = lineCommentContinues;
			continue;
		}
		for (size_t i = 0; i < line.size(); ++i) {
			const wchar_t ch = line[i];
			if (!rawStringEnd.empty()) {
				if (language == Language::Curl && curlRawOpenerRemaining > 0) {
					--curlRawOpenerRemaining;
					continue;
				}
				if (language == Language::Curl && curlRawLengthSpecified && curlRawRemaining > 0) {
					--curlRawRemaining;
					continue;
				}
				if (line.substr(i).substr(0, rawStringEnd.size()) == rawStringEnd) {
					i += rawStringEnd.size() - 1;
					rawStringEnd.clear();
					curlRawLengthSpecified = false;
					curlRawRemaining = 0;
					curlRawOpenerRemaining = 0;
				}
				continue;
			}
			if (blockComment) {
				if (ch == L'*' && i + 1 < line.size() && line[i + 1] == L'/') {
					blockComment = false;
					++i;
				}
				continue;
			}
			if (quote) {
				if (escaped) { escaped = false; continue; }
				if (ch == L'\\') { escaped = true; continue; }
				if (ch == quote) { quote = 0; continue; }
				if (language == Language::Curl && quote == L'"' && ch == L'{') {
					curlTextExpressions.push_back({ stack.size(), quote });
					quote = 0;
				} else {
					continue;
				}
			}
			if (language == Language::Curl && ch == L'|' && i + 1 < line.size() && line[i + 1] == L'|') break;
			if (language == Language::Curl && ch == L'|') {
				std::size_t bodyLength = std::numeric_limits<std::size_t>::max();
				std::size_t openerLength = 0;
				std::wstring terminator;
				if (curlsyntax::TryGetCommentTerminator(line, i, terminator, bodyLength, openerLength) ||
					curlsyntax::TryGetVerbatimTerminator(line, i, terminator, bodyLength, openerLength)) {
					rawStringEnd = std::move(terminator);
					curlRawLengthSpecified = bodyLength != std::numeric_limits<std::size_t>::max();
					curlRawRemaining = bodyLength;
					curlRawOpenerRemaining = openerLength - 1;
					continue;
				}
			}
			if (language != Language::Curl && ch == L'/' && i + 1 < line.size()) {
				if (line[i + 1] == L'/') {
					lineCommentContinues = !line.empty() && line.back() == L'\\';
					break;
				}
				if (line[i + 1] == L'*') {
					blockComment = true;
					++i;
					continue;
				}
			}
			if (language != Language::Curl && ch == L'R' && i + 1 < line.size() && line[i + 1] == L'"') {
				const size_t delimiterEnd = line.find(L'(', i + 2);
				if (delimiterEnd != std::wstring_view::npos && delimiterEnd - i <= 20) {
					rawStringEnd = L")";
					rawStringEnd.append(line.substr(i + 2, delimiterEnd - (i + 2)));
					rawStringEnd += L"\"";
					i = delimiterEnd;
					continue;
			}
			}
			if (ch == L'\'' || ch == L'"' || (language != Language::Curl && ch == L'`')) {
				quote = ch;
				escaped = false;
				continue;
			}
			if (ch == L'{' && !(language == Language::Curl && IsCurlBraceEscaped(line, i))) {
				stack.push_back({ lineNo, stack.size(), BraceKind(line, language) });
			} else if (ch == L'}' && !stack.empty() && !(language == Language::Curl && IsCurlBraceEscaped(line, i))) {
				const OpenBlock open = std::move(stack.back());
				stack.pop_back();
				AddClosedRegion(regions, open, lineNo);
				if (language == Language::Curl && !curlTextExpressions.empty() &&
					stack.size() == curlTextExpressions.back().baseDepth) {
					quote = curlTextExpressions.back().quote;
					curlTextExpressions.pop_back();
				}
			}
		}
		continuedQuote = 0;
		if (quote == L'`' || (language == Language::Curl && quote == L'"') ||
			(language != Language::Curl && quote && escaped)) {
			continuedQuote = quote;
		}
		continuedLineComment = lineCommentContinues;
	}
	return regions;
}

bool IsNameChar(wchar_t ch)
{
	return std::iswalnum(ch) || ch == L'_' || ch == L':' || ch == L'-' || ch == L'.';
}

std::vector<Region> ParseXml(const std::vector<std::wstring_view>& lines)
{
	std::vector<Region> regions;
	std::vector<std::pair<std::wstring, OpenBlock>> stack;
	bool comment = false;
	bool inTag = false;
	wchar_t quote = 0;
	std::wstring tag;
	for (size_t lineNo = 0; lineNo < lines.size(); ++lineNo) {
		const auto line = lines[lineNo];
		for (size_t i = 0; i < line.size(); ++i) {
			if (comment) {
				if (line.substr(i, 3) == L"-->") { comment = false; i += 2; }
				continue;
			}
			if (!inTag) {
				if (line.substr(i, 4) == L"<!--") { comment = true; i += 3; }
				else if (line[i] == L'<') { inTag = true; tag.clear(); }
				continue;
			}
			const wchar_t ch = line[i];
			if (quote) {
				tag.push_back(ch);
				if (ch == quote) quote = 0;
				continue;
			}
			if (ch == L'\'' || ch == L'"') { quote = ch; tag.push_back(ch); continue; }
			if (ch != L'>') { tag.push_back(ch); continue; }
			inTag = false;
			std::wstring_view body(tag);
			while (!body.empty() && std::iswspace(body.front())) body.remove_prefix(1);
			if (body.empty() || body.front() == L'!' || body.front() == L'?') continue;
			const bool closing = body.front() == L'/';
			if (closing) body.remove_prefix(1);
			while (!body.empty() && std::iswspace(body.front())) body.remove_prefix(1);
			size_t nameLength = 0;
			while (nameLength < body.size() && IsNameChar(body[nameLength])) ++nameLength;
			if (nameLength == 0) continue;
			const std::wstring name(body.substr(0, nameLength));
			if (closing) {
				if (!stack.empty() && stack.back().first == name) {
					const OpenBlock open = std::move(stack.back().second);
					stack.pop_back();
					AddClosedRegion(regions, open, lineNo);
				}
				continue;
			}
			size_t end = body.size();
			while (end > 0 && std::iswspace(body[end - 1])) --end;
			if (end > 0 && body[end - 1] == L'/') continue;
			stack.push_back({ name, { lineNo, stack.size(), name } });
		}
	}
	return regions;
}

std::wstring Upper(std::wstring_view value)
{
	std::wstring result(value);
	std::transform(result.begin(), result.end(), result.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
	return result;
}

std::vector<Region> ParseSql(const std::vector<std::wstring_view>& lines)
{
	std::vector<Region> regions;
	std::vector<OpenBlock> stack;
	bool blockComment = false;
	wchar_t quote = 0;
	wchar_t alternativeQuoteEnd = 0;
	std::vector<std::wstring> tokens;
	tokens.reserve(16);
	for (size_t lineNo = 0; lineNo < lines.size(); ++lineNo) {
		tokens.clear();
		const auto line = lines[lineNo];
		for (size_t i = 0; i < line.size();) {
			const wchar_t ch = line[i];
			if (alternativeQuoteEnd) {
				if (ch == alternativeQuoteEnd && i + 1 < line.size() && line[i + 1] == L'\'') {
					alternativeQuoteEnd = 0;
					i += 2;
				} else {
					++i;
				}
				continue;
			}
			if (blockComment) {
				if (ch == L'*' && i + 1 < line.size() && line[i + 1] == L'/') { blockComment = false; i += 2; }
				else ++i;
				continue;
			}
			if (quote) {
				if (line[i] == quote) {
					if (i + 1 < line.size() && line[i + 1] == quote) i += 2;
					else { quote = 0; ++i; }
				} else {
				++i;
				}
				continue;
			}
			if (ch == L'-' && i + 1 < line.size() && line[i + 1] == L'-') break;
			if (ch == L'/' && i + 1 < line.size() && line[i + 1] == L'*') { blockComment = true; i += 2; continue; }
			if ((ch == L'q' || ch == L'Q') && i + 2 < line.size() && line[i + 1] == L'\'') {
				const wchar_t delimiter = line[i + 2];
				if (!std::iswspace(delimiter) && delimiter != L'\'') {
					switch (delimiter) {
					case L'[': alternativeQuoteEnd = L']'; break;
					case L'{': alternativeQuoteEnd = L'}'; break;
					case L'(': alternativeQuoteEnd = L')'; break;
					case L'<': alternativeQuoteEnd = L'>'; break;
					default: alternativeQuoteEnd = delimiter; break;
					}
					i += 3;
					continue;
				}
			}
			if (ch == L'\'' || ch == L'"') {
				quote = ch;
				++i;
				continue;
			}
			if (std::iswalpha(ch) || ch == L'_') {
				const size_t begin = i++;
				while (i < line.size() && (std::iswalnum(line[i]) || line[i] == L'_')) ++i;
				tokens.push_back(Upper(line.substr(begin, i - begin)));
				continue;
			}
			++i;
		}
		for (size_t i = 0; i < tokens.size(); ++i) {
			const auto& token = tokens[i];
			if (token == L"END") {
				std::wstring expected;
				if (i + 1 < tokens.size() && (tokens[i + 1] == L"IF" || tokens[i + 1] == L"LOOP" || tokens[i + 1] == L"CASE")) {
					expected = tokens[++i];
				}
				const std::wstring_view required = expected.empty() ? std::wstring_view(L"BEGIN") : expected;
				if (!stack.empty() && stack.back().kind == required) {
					const OpenBlock open = std::move(stack.back());
					stack.pop_back();
					AddClosedRegion(regions, open, lineNo);
				}
				continue;
			}
			if (token == L"BEGIN" || token == L"IF" || token == L"LOOP" || token == L"CASE") {
				stack.push_back({ lineNo, stack.size(), token });
			}
		}
		if (tokens.capacity() > 1024) std::vector<std::wstring>().swap(tokens);
	}
	return regions;
}
}

std::vector<Region> Parse(const std::vector<std::wstring_view>& lines, Language language)
{
	std::vector<Region> regions;
	switch (language) {
	case Language::Curl:
	case Language::CLike:
		regions = ParseBraces(lines, language);
		break;
	case Language::Xml:
		regions = ParseXml(lines);
		break;
	case Language::Sql:
		regions = ParseSql(lines);
		break;
	}
	std::stable_sort(regions.begin(), regions.end(), [](const Region& left, const Region& right) {
		if (left.firstLine != right.firstLine) return left.firstLine < right.firstLine;
		if (left.lastLine != right.lastLine) return left.lastLine > right.lastLine;
		return left.depth < right.depth;
	});
	return regions;
}
}
