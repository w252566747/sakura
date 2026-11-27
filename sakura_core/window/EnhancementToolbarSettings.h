#ifndef SAKURA_ENHANCEMENTTOOLBARSETTINGS_H_
#define SAKURA_ENHANCEMENTTOOLBARSETTINGS_H_
#pragma once

#include <cstdint>

namespace enhancementtoolbar {
	enum class Group : std::uint32_t {
		CurlSyntax = 1u << 0,
		RainbowBracket = 1u << 1,
		MarkerNavigation = 1u << 2,
		RealtimeFilter = 1u << 3,
		CSVTable = 1u << 4,
		CodeFolding = 1u << 5,
		MultiCursor = 1u << 6,
	};

	inline constexpr std::uint32_t DEFAULT_VISIBILITY_MASK = (1u << 7) - 1u;
	inline constexpr std::uint32_t KNOWN_VISIBILITY_MASK = DEFAULT_VISIBILITY_MASK;
	inline constexpr std::uint32_t DEFAULT_ENABLED_MASK = KNOWN_VISIBILITY_MASK;
	inline constexpr std::uint32_t KNOWN_ENABLED_MASK = KNOWN_VISIBILITY_MASK;

	inline std::uint32_t& VisibilityMaskStorage() noexcept
	{
		static std::uint32_t mask = DEFAULT_VISIBILITY_MASK;
		return mask;
	}

	inline std::uint32_t GetVisibilityMask() noexcept
	{
		return VisibilityMaskStorage();
	}

	inline void SetVisibilityMask(std::uint32_t mask) noexcept
	{
		VisibilityMaskStorage() = mask & KNOWN_VISIBILITY_MASK;
	}

	inline bool IsVisible(Group group) noexcept
	{
		return (GetVisibilityMask() & static_cast<std::uint32_t>(group)) != 0;
	}

	inline void SetVisible(Group group, bool visible) noexcept
	{
		const auto bit = static_cast<std::uint32_t>(group);
		auto& mask = VisibilityMaskStorage();
		mask = visible ? (mask | bit) : (mask & ~bit);
	}

	inline std::uint32_t& EnabledMaskStorage() noexcept
	{
		static std::uint32_t mask = DEFAULT_ENABLED_MASK;
		return mask;
	}

	inline std::uint32_t GetEnabledMask() noexcept { return EnabledMaskStorage(); }

	inline void SetEnabledMask(std::uint32_t mask) noexcept
	{
		EnabledMaskStorage() = mask & KNOWN_ENABLED_MASK;
	}

	inline bool IsEnabled(Group group) noexcept
	{
		return (GetEnabledMask() & static_cast<std::uint32_t>(group)) != 0;
	}
}

#endif // SAKURA_ENHANCEMENTTOOLBARSETTINGS_H_
