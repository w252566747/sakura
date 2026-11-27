/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_BRACKETNESTINGSTATE_H_
#define SAKURA_BRACKETNESTINGSTATE_H_
#pragma once

#include <cstdint>
#include <cwctype>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "curl/CurlRawRegion.h"

struct SRainbowBracketMark {
	int position = 0;
	unsigned char level = 0;
	bool unmatched = false;
};

inline bool IsRainbowBracketCharacter(wchar_t ch) noexcept
{
	return ch == L'(' || ch == L')' || ch == L'[' || ch == L']' || ch == L'{' || ch == L'}';
}

inline bool IsEscapedCurlCharacter(std::wstring_view line, std::size_t position) noexcept
{
	if (position >= line.size()) return false;
	std::size_t backslashes = 0;
	while (position > backslashes && line[position - backslashes - 1] == L'\\') ++backslashes;
	return (backslashes & 1u) != 0;
}

//! Curl escapes reserved braces with a backslash; an odd escape run means the brace is text.
inline bool IsEscapedCurlBrace(std::wstring_view line, std::size_t position) noexcept
{
	if (position >= line.size() || (line[position] != L'{' && line[position] != L'}')) return false;
	return IsEscapedCurlCharacter(line, position);
}

inline bool IsMatchingRainbowBracketPair(const SRainbowBracketMark& firstMark, wchar_t firstBracket,
	const SRainbowBracketMark& secondMark, wchar_t secondBracket) noexcept
{
	if (firstMark.unmatched || secondMark.unmatched || firstMark.level != secondMark.level) return false;
	return (firstBracket == L'(' && secondBracket == L')') || (firstBracket == L')' && secondBracket == L'(') ||
		(firstBracket == L'[' && secondBracket == L']') || (firstBracket == L']' && secondBracket == L'[') ||
		(firstBracket == L'{' && secondBracket == L'}') || (firstBracket == L'}' && secondBracket == L'{');
}

inline bool ShouldHighlightRainbowBracketMark(const SRainbowBracketMark& mark, int currentLevel,
	bool highlightCurrentLevel, bool highlightUnmatched) noexcept
{
	return (mark.unmatched && highlightUnmatched)
		|| (highlightCurrentLevel && currentLevel >= 0 && mark.level == static_cast<unsigned>(currentLevel));
}

//! Persistent bracket stack carried between logical lines by the layout cache.
class CBracketNestingState final {
public:
	static constexpr unsigned COLOR_COUNT = 6;
	void BeginCurlTextExpression() { m_curlTextExpressions.push_back({ Depth(), L'\0' }); }
	bool IsInsideCurlTextExpression() const noexcept { return !m_curlTextExpressions.empty(); }
	bool IsInsideCurlTextExpressionString() const noexcept
	{
		return !m_curlTextExpressions.empty() && m_curlTextExpressions.back().quote != L'\0';
	}
	wchar_t GetCurlTextExpressionQuote() const noexcept
	{
		return m_curlTextExpressions.empty() ? L'\0' : m_curlTextExpressions.back().quote;
	}
	void ToggleCurlTextExpressionString(wchar_t quote) noexcept
	{
		if (!m_curlTextExpressions.empty()) {
			auto& current = m_curlTextExpressions.back().quote;
			current = current == quote ? L'\0' : quote;
		}
	}
	void EndCurlTextExpressionIfBalanced() noexcept
	{
		if (!m_curlTextExpressions.empty() && Depth() == m_curlTextExpressions.back().baseDepth) {
			m_curlTextExpressions.pop_back();
		}
	}
	void BeginCurlVerbatim(std::wstring terminator, std::size_t bodyLength = std::numeric_limits<std::size_t>::max(),
		std::size_t openerTailLength = 0)
	{
		m_curlVerbatimTerminator = std::move(terminator);
		if (m_curlVerbatimTerminator.empty()) {
			m_curlVerbatimPrefix.clear();
			m_curlVerbatimMatched = 0;
			m_curlVerbatimLengthSpecified = false;
			m_curlVerbatimRemaining = 0;
			m_curlVerbatimOpenerRemaining = 0;
			return;
		}
		m_curlVerbatimPrefix.assign(m_curlVerbatimTerminator.size(), 0);
		for (std::size_t i = 1, matched = 0; i < m_curlVerbatimTerminator.size(); ++i) {
			while (matched > 0 && m_curlVerbatimTerminator[i] != m_curlVerbatimTerminator[matched])
				matched = m_curlVerbatimPrefix[matched - 1];
			if (m_curlVerbatimTerminator[i] == m_curlVerbatimTerminator[matched]) ++matched;
			m_curlVerbatimPrefix[i] = matched;
		}
		m_curlVerbatimMatched = 0;
		m_curlVerbatimLengthSpecified = bodyLength != std::numeric_limits<std::size_t>::max();
		m_curlVerbatimRemaining = bodyLength;
		m_curlVerbatimOpenerRemaining = openerTailLength;
	}
	bool IsInsideCurlVerbatim() const noexcept { return !m_curlVerbatimTerminator.empty(); }
	//! Consume raw verbatim text; true means this character must not reach bracket parsing.
	bool ConsumeCurlVerbatim(wchar_t ch)
	{
		if (m_curlVerbatimTerminator.empty()) return false;
		if (m_curlVerbatimOpenerRemaining > 0) {
			--m_curlVerbatimOpenerRemaining;
			return true;
		}
		if (m_curlVerbatimLengthSpecified && m_curlVerbatimRemaining > 0) {
			--m_curlVerbatimRemaining;
			return true;
		}
		while (m_curlVerbatimMatched > 0 && ch != m_curlVerbatimTerminator[m_curlVerbatimMatched])
			m_curlVerbatimMatched = m_curlVerbatimPrefix[m_curlVerbatimMatched - 1];
		if (ch == m_curlVerbatimTerminator[m_curlVerbatimMatched]) ++m_curlVerbatimMatched;
		if (m_curlVerbatimMatched == m_curlVerbatimTerminator.size()) {
			m_curlVerbatimTerminator.clear();
			m_curlVerbatimPrefix.clear();
			m_curlVerbatimMatched = 0;
			m_curlVerbatimLengthSpecified = false;
			m_curlVerbatimRemaining = 0;
			m_curlVerbatimOpenerRemaining = 0;
		}
		return true;
	}

	bool Consume(wchar_t ch, unsigned* colorLevel, bool* mismatched)
	{
		if (colorLevel) *colorLevel = 0;
		if (mismatched) *mismatched = false;

		const unsigned openType = OpenType(ch);
		if (openType != 0) {
			if (colorLevel) *colorLevel = Depth() % COLOR_COUNT;
			m_top = std::make_shared<Node>(static_cast<std::uint8_t>(openType), m_top);
			return true;
		}

		const unsigned closeType = CloseType(ch);
		if (closeType == 0) return false;
		if (!m_top) {
			if (mismatched) *mismatched = true;
			return true;
		}
		const unsigned topType = m_top->type;
		if (topType != closeType) {
			if (mismatched) *mismatched = true;
			return true;
		}
		if (colorLevel) *colorLevel = (Depth() - 1) % COLOR_COUNT;
		m_top = m_top->previous;
		return true;
	}

	unsigned Depth() const noexcept { return m_top ? m_top->depth : 0; }
	bool operator==(const CBracketNestingState& other) const noexcept
	{
		if (m_curlTextExpressions != other.m_curlTextExpressions ||
			m_curlVerbatimTerminator != other.m_curlVerbatimTerminator || m_curlVerbatimMatched != other.m_curlVerbatimMatched ||
			m_curlVerbatimLengthSpecified != other.m_curlVerbatimLengthSpecified ||
			m_curlVerbatimRemaining != other.m_curlVerbatimRemaining ||
			m_curlVerbatimOpenerRemaining != other.m_curlVerbatimOpenerRemaining) return false;
		if (m_top == other.m_top) return true;
		auto left = m_top;
		auto right = other.m_top;
		while (left && right) {
			if (left->type != right->type) return false;
			left = left->previous;
			right = right->previous;
		}
		return !left && !right;
	}
	bool operator!=(const CBracketNestingState& other) const noexcept { return !(*this == other); }

private:
	struct Node {
		Node(std::uint8_t value, std::shared_ptr<const Node> parent)
			: type(value), depth(parent ? parent->depth + 1 : 1), previous(std::move(parent)) {}
		std::uint8_t type;
		unsigned depth;
		std::shared_ptr<const Node> previous;
	};
	struct CurlTextExpression {
		unsigned baseDepth;
		wchar_t quote;
		bool operator==(const CurlTextExpression& other) const noexcept
		{
			return baseDepth == other.baseDepth && quote == other.quote;
		}
		bool operator!=(const CurlTextExpression& other) const noexcept { return !(*this == other); }
	};
	static unsigned OpenType(wchar_t ch) noexcept
	{
		return ch == L'(' ? 1u : ch == L'[' ? 2u : ch == L'{' ? 3u : 0u;
	}
	static unsigned CloseType(wchar_t ch) noexcept
	{
		return ch == L')' ? 1u : ch == L']' ? 2u : ch == L'}' ? 3u : 0u;
	}

	std::shared_ptr<const Node> m_top;
	std::vector<CurlTextExpression> m_curlTextExpressions;
	std::wstring m_curlVerbatimTerminator;
	std::vector<std::size_t> m_curlVerbatimPrefix;
	std::size_t m_curlVerbatimMatched = 0;
	std::size_t m_curlVerbatimOpenerRemaining = 0;
	bool m_curlVerbatimLengthSpecified = false;
	std::size_t m_curlVerbatimRemaining = 0;
};

//! Resolves closing and EOF status for bracket marks in document order.
class CBracketMatchResolver final {
public:
	void Consume(wchar_t bracket, SRainbowBracketMark* mark)
	{
		if (!mark) return;
		mark->unmatched = false;
		if (bracket == L'(' || bracket == L'[' || bracket == L'{') {
			m_openings.push_back({ bracket, mark });
			return;
		}
		if (bracket != L')' && bracket != L']' && bracket != L'}') return;
		if (m_openings.empty() || !Matches(m_openings.back().bracket, bracket)) {
			mark->unmatched = true;
			return;
		}
		m_openings.pop_back();
	}
	void Finish() noexcept
	{
		for (const auto& opening : m_openings) opening.mark->unmatched = true;
		m_openings.clear();
	}
private:
	struct Opening { wchar_t bracket; SRainbowBracketMark* mark; };
	static bool Matches(wchar_t open, wchar_t close) noexcept
	{
		return (open == L'(' && close == L')') || (open == L'[' && close == L']') ||
			(open == L'{' && close == L'}');
	}
	std::vector<Opening> m_openings;
};

struct SRainbowBracketCache {
	CBracketNestingState begin;
	CBracketNestingState end;
	std::vector<SRainbowBracketMark> marks;
};

#endif /* SAKURA_BRACKETNESTINGSTATE_H_ */
