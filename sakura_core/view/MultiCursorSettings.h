/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_MULTICURSORSETTINGS_H_
#define SAKURA_MULTICURSORSETTINGS_H_
#pragma once

#include <cstddef>

namespace multicursor {
	inline constexpr std::size_t MAX_CURSOR_COUNT = 1000;

	inline constexpr bool IsValidCursorLimit(std::size_t count) noexcept
	{
		return count >= 1 && count <= MAX_CURSOR_COUNT;
	}
}

#endif // SAKURA_MULTICURSORSETTINGS_H_
