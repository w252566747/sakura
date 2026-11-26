#pragma once

#include "basis/SakuraBasis.h"

namespace multicursor {

inline CLogicPoint TransformPointAfterReplace(
	const CLogicPoint& point,
	const CLogicPoint& oldStart,
	const CLogicPoint& oldEnd,
	const CLogicPoint& newEnd)
{
	if (PointCompare(point, oldStart) < 0) return point;
	if (PointCompare(point, oldEnd) <= 0) return newEnd;
	if (point.y == oldEnd.y) {
		return CLogicPoint(
			CLogicInt(static_cast<int>(newEnd.x) + static_cast<int>(point.x) - static_cast<int>(oldEnd.x)),
			newEnd.y);
	}
	return CLogicPoint(
		point.x,
		CLogicInt(static_cast<int>(point.y) + static_cast<int>(newEnd.y) - static_cast<int>(oldEnd.y)));
}

}
