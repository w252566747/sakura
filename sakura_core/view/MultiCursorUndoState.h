#pragma once

#include <array>
#include <cstddef>

namespace multicursor {
	template <typename Point>
	struct UndoPosition {
		bool valid = false;
		bool hasStateMarker = false;
		Point caret{};
		Point anchor{};
		bool hasSelection = false;
	};

	template <std::size_t SlotCount, typename Point>
	void RecordUndoPosition(std::array<UndoPosition<Point>, SlotCount>& positions,
		int slot, const Point& caretBefore, const Point& caretAfter,
		const Point& anchorBefore, const Point& anchorAfter,
		bool selectedBefore, bool selectedAfter, bool stateMarker, bool redo)
	{
		if (slot < 0 || static_cast<std::size_t>(slot) >= SlotCount) return;
		auto& position = positions[static_cast<std::size_t>(slot)];
		if (stateMarker) {
			// Undo keeps the first state marker; redo keeps the last one in the group.
			if (redo || !position.hasStateMarker) {
				position.caret = redo ? caretAfter : caretBefore;
				position.anchor = redo ? anchorAfter : anchorBefore;
				position.hasSelection = redo ? selectedAfter : selectedBefore;
				position.valid = true;
				position.hasStateMarker = true;
			}
		} else if (!position.hasStateMarker && (redo || !position.valid)) {
			// Edits without a marker still restore a useful collapsed caret position.
			position.caret = redo ? caretAfter : caretBefore;
			position.anchor = position.caret;
			position.hasSelection = false;
			position.valid = true;
		}
	}
}
