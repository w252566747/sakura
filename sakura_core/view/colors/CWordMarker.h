/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CWORDMARKER_H_
#define SAKURA_CWORDMARKER_H_
#pragma once
#include <string>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

class CWordMarker final {
public:
	struct Entry { std::wstring word; unsigned color; };
	static CWordMarker& Instance();
	const std::vector<Entry>& Entries() const noexcept { return m_entries; }
	bool Toggle(std::wstring word, unsigned color);
	bool SetColor(size_t index, unsigned color) noexcept;
	unsigned NextColor(const std::vector<unsigned>& palette) const noexcept;
	void ClearFixed() noexcept;
	void SetAutomatic(const void* document, std::wstring word);
	void ClearAutomatic(const void* document) noexcept;
	void SetAutomaticEnabled(bool enabled) noexcept { m_automaticEnabled = enabled; }
	bool IsAutomaticEnabled() const noexcept { return m_automaticEnabled; }
	int Match(const void* document, const wchar_t* text, int length, int position, int* end) const noexcept;
	unsigned ColorAt(int index) const noexcept;
	void Load(const std::vector<Entry>& entries);
private:
	std::vector<Entry> m_entries;
	std::unordered_map<wchar_t, std::vector<size_t>> m_singleCharacterIndex;
	std::unordered_map<const void*, std::wstring> m_automaticByDocument;
	mutable const void* m_cachedAutomaticDocument = nullptr;
	mutable const std::wstring* m_cachedAutomatic = nullptr;
	bool m_automaticEnabled = true;
	void RebuildIndex();
};
#endif
