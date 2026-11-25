/*!
	@file CurlSyntaxChecker.cpp
	@brief Conservative Curl 8.0 lexical checks and likely built-in name typo warnings.
	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "curl/CurlSyntaxChecker.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <limits>
#include <string>
#include <unordered_set>

namespace curlsyntax {
namespace {

constexpr std::wstring_view kKnownCurlSymbols[] = {
#include "curl/CurlSyntaxKeywords.inc"
};

struct OpenDelimiter {
	wchar_t value;
	size_t offset;
	int line;
	int column;
	bool commaSeparatedChildren = false;
	bool insideMacro = false;
};

std::wstring_view ReadCurlHead(std::wstring_view source, size_t openOffset)
{
	size_t p = openOffset + 1;
	while (p < source.size() && std::iswspace(source[p])) ++p;
	if (source.substr(p, 4) == L"new " || source.substr(p, 4) == L"new\t") {
		p += 3;
		while (p < source.size() && std::iswspace(source[p])) ++p;
	}
	const size_t start = p;
	while (p < source.size() && (std::iswalnum(source[p]) || source[p] == L'_' || source[p] == L'-' || source[p] == L'?')) ++p;
	return source.substr(start, p - start);
}

bool IsCommaSeparatedContainer(std::wstring_view source, size_t openOffset)
{
	const auto head = ReadCurlHead(source, openOffset);
	return head == L"VBox" || head == L"HBox";
}

bool IsMacroDefinition(std::wstring_view source, size_t openOffset)
{
	const auto head = ReadCurlHead(source, openOffset);
	return head == L"define-syntax" || head == L"define-macro";
}

bool IsHex(wchar_t ch)
{
	return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F');
}

bool IsCanceled(const std::atomic_bool* canceled)
{
	return canceled && canceled->load(std::memory_order_relaxed);
}

bool IsAsciiIdentifier(std::wstring_view token)
{
	return !token.empty() && std::all_of(token.begin(), token.end(), [](wchar_t ch) {
		return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
			(ch >= L'0' && ch <= L'9') || ch == L'_' || ch == L'-' || ch == L'?';
	});
}

bool IsOneEditAway(std::wstring_view actual, std::wstring_view known)
{
	if (actual == known || !IsAsciiIdentifier(actual) || actual.size() < 5 || known.size() < 5 ||
		actual.size() + 1 < known.size() || known.size() + 1 < actual.size()) return false;
	size_t a = 0, b = 0;
	int edits = 0;
	while (a < actual.size() && b < known.size()) {
		const wchar_t ca = static_cast<wchar_t>(std::towlower(actual[a]));
		const wchar_t cb = static_cast<wchar_t>(std::towlower(known[b]));
		if (ca == cb) { ++a; ++b; continue; }
		if (++edits > 1) return false;
		if (actual.size() > known.size()) ++a;
		else if (known.size() > actual.size()) ++b;
		else { ++a; ++b; }
	}
	if (a < actual.size() || b < known.size()) ++edits;
	return edits == 1;
}

std::wstring_view FindLikelyTypo(std::wstring_view token)
{
	if (!IsAsciiIdentifier(token) || token.size() < 5 || token.size() >= 128) return {};
	static const std::unordered_set<std::wstring> knownSymbols = [] {
		std::unordered_set<std::wstring> symbols;
		for (const auto candidate : kKnownCurlSymbols) {
			std::wstring lower;
			lower.reserve(candidate.size());
			for (wchar_t ch : candidate) lower.push_back(static_cast<wchar_t>(std::towlower(ch)));
			symbols.insert(std::move(lower));
		}
		return symbols;
	}();
	std::wstring lowered;
	lowered.reserve(token.size());
	for (wchar_t ch : token) lowered.push_back(static_cast<wchar_t>(std::towlower(ch)));
	if (knownSymbols.find(lowered) != knownSymbols.end()) return {};
	static const auto symbolsByLength = [] {
		std::array<std::vector<std::wstring_view>, 128> groups;
		for (const auto candidate : kKnownCurlSymbols) {
			if (candidate.size() < groups.size()) groups[candidate.size()].push_back(candidate);
		}
		return groups;
	}();
	const size_t minimumLength = std::max<size_t>(5, token.size() - 1);
	const size_t maximumLength = std::min(token.size() + 1, symbolsByLength.size() - 1);
	for (size_t length = minimumLength; length <= maximumLength; ++length) {
		for (const auto candidate : symbolsByLength[length]) {
			if (IsOneEditAway(token, candidate)) return candidate;
		}
	}
	return {};
}

size_t FindSequence(std::wstring_view source, std::wstring_view sequence, size_t from,
	const std::atomic_bool* canceled)
{
	if (sequence.empty() || sequence.size() > source.size()) return std::wstring_view::npos;
	for (size_t i = from; i <= source.size() - sequence.size(); ++i) {
		if ((i & 0x3ff) == 0 && IsCanceled(canceled)) return std::wstring_view::npos;
		if (source.substr(i, sequence.size()) == sequence) return i;
	}
	return std::wstring_view::npos;
}

bool IsValidEscape(std::wstring_view text, size_t slash, size_t& end)
{
	if (slash + 1 >= text.size()) return false;
	const wchar_t ch = text[slash + 1];
	if (ch == L'u' || ch == L'U') {
		const size_t digits = ch == L'u' ? 4 : 8;
		if (slash + 2 + digits > text.size()) return false;
		for (size_t i = 0; i < digits; ++i) {
			if (!IsHex(text[slash + 2 + i])) return false;
		}
		end = slash + 2 + digits;
		return true;
	}
	constexpr std::wstring_view escaped = L"nrt'\"\\{}| =[]()";
	if (escaped.find(ch) == std::wstring_view::npos) return false;
	end = slash + 1;
	return true;
}

bool IsCommentStart(std::wstring_view s, size_t start)
{
	if (start + 1 >= s.size()) return false;
	if (s[start + 1] == L'#') return true;
	if (std::iswdigit(s[start + 1])) {
		size_t p = start + 2;
		while (p < s.size() && std::iswdigit(s[p])) ++p;
		return p < s.size() && s[p] == L'#';
	}
	if (!std::iswalpha(s[start + 1]) && s[start + 1] != L'_') return false;
	size_t p = start + 2;
	while (p < s.size() && (std::iswalnum(s[p]) || s[p] == L'_' || s[p] == L'-' || s[p] == L'?')) ++p;
	return p < s.size() && s[p] == L'#';
}

size_t FindCommentEnd(std::wstring_view s, size_t start, const std::atomic_bool* canceled)
{
	if (start + 1 >= s.size()) return std::wstring_view::npos;
	if (s[start + 1] == L'#') {
		const size_t end = FindSequence(s, L"#|", start + 2, canceled);
		return end == std::wstring_view::npos ? end : end + 1;
	}
	size_t p = start + 1;
	while (p < s.size() && std::iswdigit(s[p])) ++p;
	if (p > start + 1 && p < s.size() && s[p] == L'#') {
		size_t length = 0;
		for (size_t i = start + 1; i < p; ++i) {
			const unsigned digit = static_cast<unsigned>(s[i] - L'0');
			if (length > (std::numeric_limits<size_t>::max() - digit) / 10) return std::wstring_view::npos;
			length = length * 10 + digit;
		}
		const size_t body = p + 1;
		if (length > s.size() - body) return std::wstring_view::npos;
		const size_t end = body + length;
		std::wstring terminator = L"#";
		terminator.append(s.substr(start + 1, p - start - 1));
		terminator.push_back(L'|');
		return s.substr(end, terminator.size()) == terminator ? end + terminator.size() - 1 : std::wstring_view::npos;
	}
	if (std::iswalpha(s[start + 1]) || s[start + 1] == L'_') {
		const size_t tagStart = start + 1;
		p = tagStart + 1;
		while (p < s.size() && (std::iswalnum(s[p]) || s[p] == L'_' || s[p] == L'-' || s[p] == L'?')) ++p;
		if (p < s.size() && s[p] == L'#') {
			std::wstring terminator = L"#";
			terminator.append(s.substr(tagStart, p - tagStart));
			terminator.push_back(L'|');
			const size_t end = FindSequence(s, terminator, p + 1, canceled);
			return end == std::wstring_view::npos ? end : end + terminator.size() - 1;
		}
	}
	return std::wstring_view::npos;
}

void AddDiagnostic(std::vector<Diagnostic>& out, const char* code,
	const wchar_t* message, size_t offset, int line, int column)
{
	Diagnostic d;
	d.code = code;
	d.message = message;
	d.offset = offset;
	d.line = line;
	d.column = column;
	out.push_back(std::move(d));
}

bool IsVerbatimStart(std::wstring_view s, size_t start)
{
	if (start + 1 >= s.size()) return false;
	if (s[start + 1] == L'"') return true;
	if (std::iswdigit(s[start + 1])) {
		size_t p = start + 2;
		while (p < s.size() && std::iswdigit(s[p])) ++p;
		return p < s.size() && s[p] == L'"';
	}
	if (!std::iswalpha(s[start + 1]) && s[start + 1] != L'_') return false;
	size_t p = start + 2;
	while (p < s.size() && (std::iswalnum(s[p]) || s[p] == L'_' || s[p] == L'-')) ++p;
	return p < s.size() && s[p] == L'"';
}

size_t FindVerbatimEnd(std::wstring_view s, size_t start, const std::atomic_bool* canceled)
{
	// Regular verbatim string: |" ... "|
	if (start + 1 < s.size() && s[start + 1] == L'"') {
		const size_t end = FindSequence(s, L"\"|", start + 2, canceled);
		return end == std::wstring_view::npos ? end : end + 1;
	}

	// Length-specified form: |N" ... "N|.
	size_t p = start + 1;
	while (p < s.size() && std::iswdigit(s[p])) ++p;
	if (p > start + 1 && p < s.size() && s[p] == L'"') {
		std::wstring terminator = L"\"";
		terminator.append(s.substr(start + 1, p - start - 1));
		terminator.push_back(L'|');
		size_t length = 0;
		for (size_t i = start + 1; i < p; ++i) {
			const unsigned digit = static_cast<unsigned>(s[i] - L'0');
			if (length > (std::numeric_limits<size_t>::max() - digit) / 10) return std::wstring_view::npos;
			length = length * 10 + digit;
		}
		const size_t body = p + 1;
		if (length > s.size() - body) return std::wstring_view::npos;
		const size_t end = body + length;
		return s.substr(end, terminator.size()) == terminator ? end + terminator.size() - 1 : std::wstring_view::npos;
	}

	// Tagged verbatim form: |tag" ... "tag|.
	p = start + 1;
	if (p < s.size() && (std::iswalpha(s[p]) || s[p] == L'_')) {
		const size_t tagStart = p++;
		while (p < s.size() && (std::iswalnum(s[p]) || s[p] == L'_' || s[p] == L'-')) ++p;
		if (p < s.size() && s[p] == L'"') {
			std::wstring terminator = L"\"";
			terminator.append(s.substr(tagStart, p - tagStart));
			terminator.push_back(L'|');
			const size_t end = FindSequence(s, terminator, p + 1, canceled);
			return end == std::wstring_view::npos ? end : end + terminator.size() - 1;
		}
	}
	return std::wstring_view::npos;
}

} // namespace

std::vector<Diagnostic> Check(std::wstring_view source, const std::atomic_bool* canceled)
{
	std::vector<Diagnostic> diagnostics;
	std::vector<OpenDelimiter> delimiters;
	int curlBraceDepth = 0;
	bool suppressDelimiterCascade = false;
	int line = 1;
	int column = 1;
	wchar_t previousSignificant = L'\0';
	bool commaPending = false;
	auto advance = [&](wchar_t ch) {
		if (ch == L'\n') { ++line; column = 1; }
		else if (ch != L'\r') { ++column; }
	};

	for (size_t i = 0; i < source.size();) {
		if ((i & 0x3ff) == 0 && IsCanceled(canceled)) return {};
		const wchar_t ch = source[i];

		// Curl comments begin with || and continue to the line ending.
		if (ch == L'|' && i + 1 < source.size() && source[i + 1] == L'|') {
			while (i < source.size() && source[i] != L'\r' && source[i] != L'\n') advance(source[i++]);
			continue;
		}
		if (ch == L'|' && IsCommentStart(source, i)) {
			const int startLine = line;
			const int startColumn = column;
			const size_t end = FindCommentEnd(source, i, canceled);
			if (IsCanceled(canceled)) return {};
			if (end == std::wstring_view::npos) {
				AddDiagnostic(diagnostics, "CURL010", L"コメントが閉じられていません。", i, startLine, startColumn);
				suppressDelimiterCascade = true;
				while (i < source.size()) advance(source[i++]);
				break;
			}
			while (i <= end) advance(source[i++]);
			continue;
		}

		if (ch == L'|') {
			const int startLine = line;
			const int startColumn = column;
			const bool verbatim = IsVerbatimStart(source, i);
			const size_t end = FindVerbatimEnd(source, i, canceled);
				if (IsCanceled(canceled)) return {};
			if (verbatim && end != std::wstring_view::npos) {
				while (i <= end) advance(source[i++]);
				continue;
			}
			if (verbatim) {
				AddDiagnostic(diagnostics, "CURL008", L"文字列が閉じられていません。", i, startLine, startColumn);
				suppressDelimiterCascade = true;
				while (i < source.size()) advance(source[i++]);
				break;
			}
		}

		const bool insideCurlCode = curlBraceDepth > 0;
		if (insideCurlCode && ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || ch == L'_')) {
			const size_t start = i;
			const int startLine = line;
			const int startColumn = column;
			while (i < source.size() && ((source[i] >= L'a' && source[i] <= L'z') ||
				(source[i] >= L'A' && source[i] <= L'Z') || (source[i] >= L'0' && source[i] <= L'9') ||
				source[i] == L'_' || source[i] == L'-' || source[i] == L'?')) {
				advance(source[i++]);
			}
			const std::wstring_view token = source.substr(start, i - start);
			const std::wstring_view suggestion = FindLikelyTypo(token);
			if (!suggestion.empty()) {
				Diagnostic diagnostic;
				diagnostic.severity = Severity::Warning;
				diagnostic.code = "CURL012";
				diagnostic.message = L"「";
				diagnostic.message.append(token);
				diagnostic.message += L"」は Curl 8.0 の「";
				diagnostic.message.append(suggestion);
				diagnostic.message += L"」と綴りが似ています。確認してください。";
				diagnostic.offset = start;
				diagnostic.line = startLine;
				diagnostic.column = startColumn;
				diagnostics.push_back(std::move(diagnostic));
			}
			continue;
		}
		if (insideCurlCode && ch == L',') {
			if (commaPending) {
				AddDiagnostic(diagnostics, "CURL003", L"カンマが重複しています。", i, line, column);
			}
			commaPending = true;
			previousSignificant = L',';
			advance(source[i++]);
			continue;
		}
		if (insideCurlCode && !std::iswspace(ch)) commaPending = false;
		if (insideCurlCode && (ch == L'"' || ch == L'\'')) {
			const wchar_t quote = ch;
			const size_t start = i;
			const int startLine = line;
			const int startColumn = column;
			advance(source[i++]);
			bool closed = false;
			while (i < source.size()) {
				if (source[i] == L'\\') {
					size_t escapeEnd = i;
					if (!IsValidEscape(source, i, escapeEnd)) {
						AddDiagnostic(diagnostics, "CURL011", L"無効なエスケープシーケンスです。", i, line, column);
						advance(source[i++]);
						if (i < source.size()) advance(source[i++]);
					} else {
					while (i <= escapeEnd) advance(source[i++]);
					}
					continue;
				}
				if (source[i] == quote) {
					advance(source[i++]);
					closed = true;
					break;
				}
				// Double-quoted Curl strings can span lines; a character literal cannot.
				if (quote == L'\'' && (source[i] == L'\r' || source[i] == L'\n')) break;
				advance(source[i++]);
			}
			if (!closed) {
				Diagnostic d;
				d.code = quote == L'"' ? "CURL008" : "CURL009";
				d.message = quote == L'"' ? L"文字列が閉じられていません。" : L"文字リテラルが閉じられていません。";
				d.offset = start;
				d.line = startLine;
				d.column = startColumn;
				diagnostics.push_back(std::move(d));
				suppressDelimiterCascade = true;
			}
			previousSignificant = quote;
			continue;
		}

		if (ch == L'{' || ch == L'(' || ch == L'[') {
			const bool inheritedMacro = std::any_of(delimiters.begin(), delimiters.end(), [](const OpenDelimiter& open) { return open.insideMacro; });
			if (ch == L'{' && !inheritedMacro && !delimiters.empty() && delimiters.back().value == L'{' &&
				delimiters.back().commaSeparatedChildren && previousSignificant == L'}') {
				AddDiagnostic(diagnostics, "CURL001", L"カンマが不足しています。", i, line, column);
			}
			const bool macroDefinition = ch == L'{' && IsMacroDefinition(source, i);
			delimiters.push_back({ ch, i, line, column, ch == L'{' && IsCommaSeparatedContainer(source, i), inheritedMacro || macroDefinition });
			if (ch == L'{') ++curlBraceDepth;
			previousSignificant = ch;
			advance(source[i++]);
			continue;
		}
		if (ch == L'}' || ch == L')' || ch == L']') {
			const wchar_t expected = ch == L'}' ? L'{' : ch == L')' ? L'(' : L'[';
			const char* code = ch == L'}' ? "CURL005" : ch == L')' ? "CURL006" : "CURL007";
			const wchar_t* message = ch == L'}' ? L"余分な波括弧があります。" : ch == L')' ? L"丸括弧が対応していません。" : L"角括弧が対応していません。";
			if (!delimiters.empty() && delimiters.back().value != expected) {
				const wchar_t open = delimiters.back().value;
				code = open == L'{' ? "CURL004" : open == L'(' ? "CURL006" : "CURL007";
				message = open == L'{' ? L"波括弧が閉じられていません。" : open == L'(' ? L"丸括弧が対応していません。" : L"角括弧が対応していません。";
				if (open == L'{') --curlBraceDepth;
				delimiters.pop_back();
				AddDiagnostic(diagnostics, code, message, i, line, column);
			} else if (delimiters.empty()) {
				AddDiagnostic(diagnostics, code, message, i, line, column);
			} else {
				if (expected == L'{') --curlBraceDepth;
				delimiters.pop_back();
			}
			previousSignificant = ch;
			advance(source[i++]);
			continue;
		}
		if (insideCurlCode && !std::iswspace(ch)) previousSignificant = ch;
		advance(source[i++]);
	}

	for (const auto& open : delimiters) {
		if (suppressDelimiterCascade) break;
		const char* code = open.value == L'{' ? "CURL004" : open.value == L'(' ? "CURL006" : "CURL007";
		const wchar_t* message = open.value == L'{' ? L"波括弧が閉じられていません。" : open.value == L'(' ? L"丸括弧が対応していません。" : L"角括弧が対応していません。";
		AddDiagnostic(diagnostics, code, message, open.offset, open.line, open.column);
	}
	std::stable_sort(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& left, const Diagnostic& right) {
		return left.offset < right.offset;
	});
	return diagnostics;
}

} // namespace curlsyntax
