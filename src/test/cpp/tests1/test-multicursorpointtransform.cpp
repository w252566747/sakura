#include "pch.h"
#include "view/MultiCursorPointTransform.h"

TEST(MultiCursorPointTransform, LeavesPointsBeforeTheEditUnchanged)
{
	const CLogicPoint point(CLogicInt(2), CLogicInt(1));
	const auto transformed = multicursor::TransformPointAfterReplace(
		point, CLogicPoint(CLogicInt(4), CLogicInt(1)),
		CLogicPoint(CLogicInt(6), CLogicInt(1)),
		CLogicPoint(CLogicInt(5), CLogicInt(1)));
	EXPECT_EQ(PointCompare(transformed, point), 0);
}

TEST(MultiCursorPointTransform, ShiftsPointsAfterSameLineInsertion)
{
	const auto transformed = multicursor::TransformPointAfterReplace(
		CLogicPoint(CLogicInt(9), CLogicInt(0)),
		CLogicPoint(CLogicInt(4), CLogicInt(0)),
		CLogicPoint(CLogicInt(4), CLogicInt(0)),
		CLogicPoint(CLogicInt(7), CLogicInt(0)));
	EXPECT_EQ(PointCompare(transformed, CLogicPoint(CLogicInt(12), CLogicInt(0))), 0);
}

TEST(MultiCursorPointTransform, ShiftsSameLineSuffixAfterReplacement)
{
	const auto transformed = multicursor::TransformPointAfterReplace(
		CLogicPoint(CLogicInt(12), CLogicInt(2)),
		CLogicPoint(CLogicInt(5), CLogicInt(2)),
		CLogicPoint(CLogicInt(8), CLogicInt(2)),
		CLogicPoint(CLogicInt(6), CLogicInt(2)));
	EXPECT_EQ(PointCompare(transformed, CLogicPoint(CLogicInt(10), CLogicInt(2))), 0);
}

TEST(MultiCursorPointTransform, CollapsesPointsInsideReplacedRangeToNewEnd)
{
	const auto transformed = multicursor::TransformPointAfterReplace(
		CLogicPoint(CLogicInt(5), CLogicInt(2)),
		CLogicPoint(CLogicInt(3), CLogicInt(2)),
		CLogicPoint(CLogicInt(8), CLogicInt(2)),
		CLogicPoint(CLogicInt(4), CLogicInt(2)));
	EXPECT_EQ(PointCompare(transformed, CLogicPoint(CLogicInt(4), CLogicInt(2))), 0);
}

TEST(MultiCursorPointTransform, AdjustsLaterLinesAfterMultilineReplacement)
{
	const auto transformed = multicursor::TransformPointAfterReplace(
		CLogicPoint(CLogicInt(6), CLogicInt(8)),
		CLogicPoint(CLogicInt(2), CLogicInt(3)),
		CLogicPoint(CLogicInt(5), CLogicInt(6)),
		CLogicPoint(CLogicInt(1), CLogicInt(4)));
	EXPECT_EQ(PointCompare(transformed, CLogicPoint(CLogicInt(6), CLogicInt(6))), 0);
}

TEST(MultiCursorPointTransform, ShiftsSuffixOnReplacementEndLine)
{
	const auto transformed = multicursor::TransformPointAfterReplace(
		CLogicPoint(CLogicInt(15), CLogicInt(3)),
		CLogicPoint(CLogicInt(2), CLogicInt(1)),
		CLogicPoint(CLogicInt(8), CLogicInt(3)),
		CLogicPoint(CLogicInt(2), CLogicInt(5)));
	EXPECT_EQ(PointCompare(transformed, CLogicPoint(CLogicInt(9), CLogicInt(5))), 0);
}
