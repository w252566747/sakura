/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_BRACKETNESTINGSTATE_H_
#define SAKURA_BRACKETNESTINGSTATE_H_
#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

struct SRainbowBracketMark {
	int position = 0;
	unsigned char level = 0;
	bool unmatched = false;
};

inline bool IsMatchingRainbowBracketPair(const SRainbowBracketMark& firstMark, wchar_t firstBracket,
	const SRainbowBracketMark& secondMark, wchar_t secondBracket) noexcept
{
	if (firstMark.unmatched || secondMark.unmatched || firstMark.level != secondMark.level) return false;
	return (firstBracket == L'(' && secondBracket == L')') || (firstBracket == L')' && secondBracket == L'(') ||
		(firstBracket == L'[' && secondBracket == L']') || (firstBracket == L']' && secondBracket == L'[') ||
		(firstBracket == L'{' && secondBracket == L'}') || (firstBracket == L'}' && secondBracket == L'{');
}

//! Persistent bracket stack carried between logical lines by the layout cache.
class CBracketNestingState final {
public:
	static constexpr unsigned COLOR_COUNT = 6;
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
		if (colorLevel) *colorLevel = (Depth() - 1) % COLOR_COUNT;
		const unsigned topType = m_top->type;
		if (topType != closeType) {
			if (mismatched) *mismatched = true;
			return true;
		}
		m_top = m_top->previous;
		return true;
	}

	unsigned Depth() const noexcept { return m_top ? m_top->depth : 0; }
	bool operator==(const CBracketNestingState& other) const noexcept
	{
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
	static unsigned OpenType(wchar_t ch) noexcept
	{
		return ch == L'(' ? 1u : ch == L'[' ? 2u : ch == L'{' ? 3u : 0u;
	}
	static unsigned CloseType(wchar_t ch) noexcept
	{
		return ch == L')' ? 1u : ch == L']' ? 2u : ch == L'}' ? 3u : 0u;
	}

	std::shared_ptr<const Node> m_top;
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
