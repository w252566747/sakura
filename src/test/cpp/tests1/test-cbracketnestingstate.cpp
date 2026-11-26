/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "pch.h"
#include "doc/layout/BracketNestingState.h"

TEST(CBracketNestingState, CyclesSixColorsByNestingDepth)
{
	CBracketNestingState state;
	unsigned level = 99;
	bool unmatched = true;
	for (unsigned depth = 0; depth < 8; ++depth) {
		ASSERT_TRUE(state.Consume(L'(', &level, &unmatched));
		EXPECT_EQ(level, depth % CBracketNestingState::COLOR_COUNT);
		EXPECT_FALSE(unmatched);
	}
	for (int depth = 7; depth >= 0; --depth) {
		ASSERT_TRUE(state.Consume(L')', &level, &unmatched));
		EXPECT_EQ(level, static_cast<unsigned>(depth % CBracketNestingState::COLOR_COUNT));
		EXPECT_FALSE(unmatched);
	}
	EXPECT_EQ(state.Depth(), 0u);
}

TEST(CBracketNestingState, SupportsDeepNestingBeyondThirtyTwoLevels)
{
	CBracketNestingState state;
	unsigned level = 0;
	bool unmatched = false;
	for (unsigned depth = 0; depth < 128; ++depth) {
		ASSERT_TRUE(state.Consume(L'[', &level, &unmatched));
		EXPECT_EQ(level, depth % CBracketNestingState::COLOR_COUNT);
	}
	for (int depth = 127; depth >= 0; --depth) {
		ASSERT_TRUE(state.Consume(L']', &level, &unmatched));
		EXPECT_FALSE(unmatched);
		EXPECT_EQ(level, static_cast<unsigned>(depth % CBracketNestingState::COLOR_COUNT));
	}
	EXPECT_EQ(state.Depth(), 0u);
}

TEST(CBracketNestingState, MismatchedCloserDoesNotCorruptStack)
{
	CBracketNestingState state;
	unsigned level = 0;
	bool unmatched = false;
	ASSERT_TRUE(state.Consume(L'{', &level, &unmatched));
	ASSERT_TRUE(state.Consume(L'[', &level, &unmatched));
	EXPECT_TRUE(state.Consume(L')', &level, &unmatched));
	EXPECT_TRUE(unmatched);
	EXPECT_EQ(state.Depth(), 2u);
	EXPECT_TRUE(state.Consume(L']', &level, &unmatched));
	EXPECT_FALSE(unmatched);
	EXPECT_TRUE(state.Consume(L'}', &level, &unmatched));
	EXPECT_FALSE(unmatched);
	EXPECT_EQ(state.Depth(), 0u);
}

TEST(CBracketNestingState, UnmatchedClosersAreReportedAndNonBracketsIgnored)
{
	CBracketNestingState state;
	unsigned level = 0;
	bool unmatched = false;
	EXPECT_FALSE(state.Consume(L'x', &level, &unmatched));
	EXPECT_TRUE(state.Consume(L']', &level, &unmatched));
	EXPECT_TRUE(unmatched);
	EXPECT_EQ(state.Depth(), 0u);
}

TEST(CBracketNestingState, MatchingPairRequiresSameKindLevelAndMatchedEndpoints)
{
	const SRainbowBracketMark open{ 2, 1, false };
	const SRainbowBracketMark close{ 8, 1, false };
	EXPECT_TRUE(IsMatchingRainbowBracketPair(open, L'{', close, L'}'));
	EXPECT_TRUE(IsMatchingRainbowBracketPair(close, L'}', open, L'{'));
	EXPECT_FALSE(IsMatchingRainbowBracketPair(open, L'{', close, L')'));
	EXPECT_FALSE(IsMatchingRainbowBracketPair(open, L'{', SRainbowBracketMark{ 8, 2, false }, L'}'));
	EXPECT_FALSE(IsMatchingRainbowBracketPair(open, L'{', SRainbowBracketMark{ 8, 1, true }, L'}'));
}

TEST(CBracketMatchResolver, MarksUnmatchedOpenersAtEndOfDocument)
{
	SRainbowBracketMark outer{ 0, 0, false };
	SRainbowBracketMark inner{ 1, 1, false };
	SRainbowBracketMark closeInner{ 2, 1, false };
	CBracketMatchResolver resolver;
	resolver.Consume(L'(', &outer);
	resolver.Consume(L'[', &inner);
	resolver.Consume(L']', &closeInner);
	resolver.Finish();
	EXPECT_TRUE(outer.unmatched);
	EXPECT_FALSE(inner.unmatched);
	EXPECT_FALSE(closeInner.unmatched);
}

TEST(CBracketMatchResolver, KeepsMismatchedCloseFromCorruptingTheRemainingStack)
{
	SRainbowBracketMark outer{ 0, 0, false };
	SRainbowBracketMark inner{ 1, 1, false };
	SRainbowBracketMark wrongClose{ 2, 1, false };
	SRainbowBracketMark closeInner{ 3, 1, false };
	CBracketMatchResolver resolver;
	resolver.Consume(L'(', &outer);
	resolver.Consume(L'[', &inner);
	resolver.Consume(L')', &wrongClose);
	resolver.Consume(L']', &closeInner);
	resolver.Finish();
	EXPECT_TRUE(outer.unmatched);
	EXPECT_FALSE(inner.unmatched);
	EXPECT_TRUE(wrongClose.unmatched);
	EXPECT_FALSE(closeInner.unmatched);
}
