#pragma once

#include <algorithm>
#include <cstddef>
#include <string>

namespace csvtable::snapshot {

// Append one bounded piece of a source line while keeping the eventual snapshot
// within the document-size limit used by the Win32 table model.
inline bool AppendNextChunk(
	std::wstring& snapshot,
	const wchar_t* source,
	std::size_t sourceLength,
	std::size_t& sourceOffset,
	std::size_t maxChunkLength,
	std::size_t maxSnapshotLength)
{
	if (sourceOffset > sourceLength || (sourceLength > 0 && source == nullptr) || maxChunkLength == 0) return false;
	const std::size_t remaining = sourceLength - sourceOffset;
	if (snapshot.size() > maxSnapshotLength || remaining > maxSnapshotLength - snapshot.size()) return false;
	const std::size_t chunkLength = std::min(remaining, maxChunkLength);
	if (chunkLength > 0) snapshot.append(source + sourceOffset, chunkLength);
	sourceOffset += chunkLength;
	return true;
}

} // namespace csvtable::snapshot
