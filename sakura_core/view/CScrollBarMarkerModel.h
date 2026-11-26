/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CSCROLLBARMARKERMODEL_H_
#define SAKURA_CSCROLLBARMARKERMODEL_H_
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

/** Scrollbar overview data, independent of window handles and document storage. */
class CScrollBarMarkerModel final {
public:
	enum class Kind : std::uint8_t {
		Search,
		WordMarker,
		SyntaxError,
		SyntaxWarning,
		Bookmark,
		Modified,
	};

	struct Marker {
		int line = 0;                 // zero-based line in the overview coordinate space
		Kind kind = Kind::Search;
		std::uint32_t color = 0;      // 0x00RRGGBB, independent of Win32 COLORREF
		std::wstring label;
		std::wstring detail;
	};

	struct Tick {
		int y = 0;
		int lane = 0;                // stable column per source kind
		Kind kind = Kind::Search;
		std::uint32_t color = 0;
		int firstLine = 0;
		int lastLine = 0;
		std::size_t count = 0;
		std::wstring tooltip;
	};

	void SetMarkers(std::vector<Marker> markers)
	{
		markers.erase(std::remove_if(markers.begin(), markers.end(), [](const Marker& marker) {
			return marker.line < 0;
		}), markers.end());
		std::stable_sort(markers.begin(), markers.end(), [](const Marker& lhs, const Marker& rhs) {
			return std::tie(lhs.line, lhs.kind, lhs.color, lhs.label, lhs.detail) <
				std::tie(rhs.line, rhs.kind, rhs.color, rhs.label, rhs.detail);
		});
		m_markers = std::move(markers);
	}

	/** Start a compact overview scan. Memory use is bounded by track pixels and
	 * distinct colors instead of the number of matching lines in the document. */
	void ResetOverview(int trackHeight, int lineCount)
	{
		m_overviewTrackHeight = std::max(0, trackHeight);
		m_overviewLineCount = std::max(0, lineCount);
		m_overviewByKey.clear();
		m_overviewTicks.clear();
		m_overviewTicksDirty = false;
	}

	void AddOverviewMarker(const Marker& marker)
	{
		if (m_overviewTrackHeight <= 0 || m_overviewLineCount <= 0 || marker.line < 0) return;
		const int line = std::min(marker.line, m_overviewLineCount - 1);
		const int y = m_overviewLineCount == 1 || m_overviewTrackHeight == 1
			? 0
			: static_cast<int>((static_cast<std::uint64_t>(line) *
				static_cast<std::uint64_t>(m_overviewTrackHeight - 1)) /
				static_cast<std::uint64_t>(m_overviewLineCount - 1));
		const auto key = std::make_tuple(y, marker.kind, marker.color);
		auto found = m_overviewByKey.find(key);
		if (found == m_overviewByKey.end()) {
			Tick tick;
			tick.y = y;
			tick.lane = static_cast<int>(marker.kind);
			tick.kind = marker.kind;
			tick.color = marker.color;
			tick.firstLine = line;
			tick.lastLine = line;
			tick.count = 1;
			tick.tooltip = marker.label;
			if (!marker.detail.empty()) {
				if (!tick.tooltip.empty()) tick.tooltip += L"\n";
				tick.tooltip += marker.detail;
			}
			m_overviewByKey.emplace(key, std::move(tick));
		} else {
			Tick& tick = found->second;
			tick.firstLine = std::min(tick.firstLine, line);
			tick.lastLine = std::max(tick.lastLine, line);
			++tick.count;
			if (tick.count <= 8 && !marker.label.empty() && tick.tooltip.find(marker.label) == std::wstring::npos) {
				if (!tick.tooltip.empty()) tick.tooltip += L"\n";
				tick.tooltip += marker.label;
			}
			if (tick.count <= 8 && !marker.detail.empty() && tick.tooltip.find(marker.detail) == std::wstring::npos) {
				if (!tick.tooltip.empty()) tick.tooltip += L"\n";
				tick.tooltip += marker.detail;
			}
		}
		m_overviewTicksDirty = true;
	}

	const std::vector<Tick>& OverviewTicks() const
	{
		if (m_overviewTicksDirty) {
			m_overviewTicks.clear();
			m_overviewTicks.reserve(m_overviewByKey.size());
			for (const auto& entry : m_overviewByKey) m_overviewTicks.push_back(entry.second);
			m_overviewTicksDirty = false;
		}
		return m_overviewTicks;
	}

	const std::vector<Marker>& Markers() const noexcept { return m_markers; }

	std::vector<Tick> BuildTicks(int trackHeight, int lineCount) const
	{
		std::vector<Tick> ticks;
		std::map<std::tuple<int, Kind, std::uint32_t>, std::size_t> tickIndex;
		if (trackHeight <= 0 || lineCount <= 0) return ticks;
		ticks.reserve(std::min(m_markers.size(), static_cast<std::size_t>(trackHeight) * 6));

		for (const Marker& marker : m_markers) {
			if (marker.line >= lineCount) continue;
			const int y = lineCount == 1 || trackHeight == 1
				? 0
				: static_cast<int>((static_cast<std::uint64_t>(marker.line) *
					static_cast<std::uint64_t>(trackHeight - 1)) /
					static_cast<std::uint64_t>(lineCount - 1));
			const auto key = std::make_tuple(y, marker.kind, marker.color);
			auto found = tickIndex.find(key);
			if (found == tickIndex.end()) {
				Tick tick;
				tick.y = y;
				tick.lane = static_cast<int>(marker.kind);
				tick.kind = marker.kind;
				tick.color = marker.color;
				tick.firstLine = marker.line;
				tick.lastLine = marker.line;
				tick.count = 1;
				tick.tooltip = marker.label;
				if (!marker.detail.empty()) {
					if (!tick.tooltip.empty()) tick.tooltip += L"\n";
					tick.tooltip += marker.detail;
				}
				ticks.push_back(std::move(tick));
				tickIndex.emplace(key, ticks.size() - 1);
			} else {
				Tick& tick = ticks[found->second];
				tick.firstLine = std::min(tick.firstLine, marker.line);
				tick.lastLine = std::max(tick.lastLine, marker.line);
				++tick.count;
				if (tick.count <= 8 && !marker.label.empty() && tick.tooltip.find(marker.label) == std::wstring::npos) {
					if (!tick.tooltip.empty()) tick.tooltip += L"\n";
					tick.tooltip += marker.label;
				}
				if (tick.count <= 8 && !marker.detail.empty() && tick.tooltip.find(marker.detail) == std::wstring::npos) {
					if (!tick.tooltip.empty()) tick.tooltip += L"\n";
					tick.tooltip += marker.detail;
				}
			}
		}
		std::stable_sort(ticks.begin(), ticks.end(), [](const Tick& lhs, const Tick& rhs) {
			return std::tie(lhs.y, lhs.lane, lhs.color) < std::tie(rhs.y, rhs.lane, rhs.color);
		});
		return ticks;
	}

	static int LineAt(int y, int trackHeight, int lineCount) noexcept
	{
		if (lineCount <= 1 || trackHeight <= 1) return 0;
		y = std::max(0, std::min(y, trackHeight - 1));
		return static_cast<int>((static_cast<std::uint64_t>(y) *
			static_cast<std::uint64_t>(lineCount - 1)) /
			static_cast<std::uint64_t>(trackHeight - 1));
	}

	static std::size_t HitTest(const std::vector<Tick>& ticks, int y, int radius = 2) noexcept
	{
		return HitTestImpl(ticks, -1, y, radius);
	}

	static std::size_t HitTestLane(const std::vector<Tick>& ticks, int lane, int y, int radius = 2) noexcept
	{
		return HitTestImpl(ticks, lane, y, radius);
	}

private:
	static std::size_t HitTestImpl(const std::vector<Tick>& ticks, int lane, int y, int radius) noexcept
	{
		const auto first = std::lower_bound(ticks.begin(), ticks.end(), y,
			[](const Tick& tick, int value) { return tick.y < value; });
		auto best = ticks.size();
		int bestDistance = std::max(0, radius) + 1;
		const auto consider = [&](auto it) {
			const int distance = it->y > y ? it->y - y : y - it->y;
			if ((lane < 0 || it->lane == lane) && distance < bestDistance) {
				bestDistance = distance;
				best = static_cast<std::size_t>(it - ticks.begin());
			}
		};
		for (auto it = first; it != ticks.end() && it->y - y <= radius; ++it) consider(it);
		for (auto it = first; it != ticks.begin();) {
			--it;
			if (y - it->y > radius) break;
			consider(it);
		}
		return best;
	}

	std::vector<Marker> m_markers;
	int m_overviewTrackHeight = 0;
	int m_overviewLineCount = 0;
	std::map<std::tuple<int, Kind, std::uint32_t>, Tick> m_overviewByKey;
	mutable std::vector<Tick> m_overviewTicks;
	mutable bool m_overviewTicksDirty = false;
};

#endif // SAKURA_CSCROLLBARMARKERMODEL_H_
