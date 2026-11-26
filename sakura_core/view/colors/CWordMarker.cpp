/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "CWordMarker.h"
#include "charset/charcode.h"
#include <algorithm>
#include <climits>
#include <cwchar>
#include <utility>

CWordMarker& CWordMarker::Instance()
{
	static CWordMarker instance;
	return instance;
}

bool CWordMarker::Toggle(std::wstring word, unsigned color)
{
	if (word.empty() || word.size() > static_cast<size_t>(INT_MAX) ||
		word.find(L'\0') != std::wstring::npos || word.find(L'\r') != std::wstring::npos || word.find(L'\n') != std::wstring::npos) return false;
	const auto found = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& entry) {
		return entry.word == word;
	});
	if (found != m_entries.end()) {
		m_entries.erase(found);
		RebuildIndex();
		++m_generation;
		return true;
	}
	if (m_entries.size() >= 32) return false;
	m_entries.push_back({ std::move(word), color });
	RebuildIndex();
	++m_generation;
	return true;
}

bool CWordMarker::SetColor(size_t index, unsigned color) noexcept
{
	if (index >= m_entries.size()) return false;
	m_entries[index].color = color;
	++m_generation;
	return true;
}

bool CWordMarker::ToggleEnabled(size_t index) noexcept
{
	if (index >= m_entries.size()) return false;
	m_entries[index].enabled = !m_entries[index].enabled;
	++m_generation;
	return true;
}

unsigned CWordMarker::NextColor(const std::vector<unsigned>& palette,
	const std::vector<unsigned>& unavailableColors) const noexcept
{
	for (size_t index = 0; index < palette.size(); ++index) {
		const auto color = palette[index];
		// Relative color zero maps to COLORIDX_SEARCH (yellow), reserved for Find.
		if (color == 0) continue;
		if (std::find(palette.begin(), palette.begin() + index, color) != palette.begin() + index) continue;
		if (std::none_of(m_entries.begin(), m_entries.end(), [color](const Entry& entry) { return entry.color == color; })) return color;
	}
	// Continue with distinct pastel colors once the user's configured Sakura
	// search palette is exhausted. These values are stored in the existing INI
	// color field and do not require a shared-memory layout change.
	static constexpr unsigned customColors[] = {
		0x00d7f2ff, 0x00c8e7ff, 0x00d8dcff, 0x00e9d7ff, 0x00f5d5ff, 0x00f7d9e8,
		0x00d7e8f3, 0x00d7f3e8, 0x00e6f3d7, 0x00f4edcf, 0x00f6dfcf, 0x00e5e0d8,
		0x00c9d9e8, 0x00d6d2ee, 0x00edcee4, 0x00e6cfcf, 0x00cfe8dc, 0x00e4edc8,
		0x00f0e4c8, 0x00d3e3f5, 0x00e5d9f5, 0x00f2d9f0, 0x00f0d7d7, 0x00d7eee8,
		0x00e2edda, 0x00f3eccf, 0x00f1dfd3, 0x00d4e1e4, 0x00ded8e8, 0x00e8dce0,
		0x00cfe2ef, 0x00e5edda
	};
	for (const unsigned color : customColors) {
		const unsigned encoded = EncodeCustomColor(color);
		if (std::none_of(m_entries.begin(), m_entries.end(), [encoded](const Entry& entry) { return entry.color == encoded; }) &&
			std::find(unavailableColors.begin(), unavailableColors.end(), color) == unavailableColors.end()) return encoded;
	}
	return EncodeCustomColor(0x00d7f2ff);
}

void CWordMarker::ClearFixed() noexcept
{
	m_entries.clear();
	m_singleCharacterIndex.clear();
	++m_generation;
}

void CWordMarker::RebuildIndex()
{
	m_singleCharacterIndex.clear();
	for (size_t index = 0; index < m_entries.size(); ++index) {
		const auto& word = m_entries[index].word;
		m_singleCharacterIndex[word.front()].push_back(index);
	}
}

void CWordMarker::SetAutomatic(const void* document, std::wstring word)
{
	if (document == nullptr || word.empty() || word.size() > static_cast<size_t>(INT_MAX) ||
		word.find(L'\0') != std::wstring::npos || word.find(L'\r') != std::wstring::npos || word.find(L'\n') != std::wstring::npos) {
		ClearAutomatic(document);
		return;
	}
	m_automaticByDocument[document] = std::move(word);
	m_cachedAutomaticDocument = nullptr;
	m_cachedAutomatic = nullptr;
	++m_generation;
}

void CWordMarker::ClearAutomatic(const void* document) noexcept
{
	m_automaticByDocument.erase(document);
	m_cachedAutomaticDocument = nullptr;
	m_cachedAutomatic = nullptr;
	++m_generation;
}

void CWordMarker::SetAutomaticEnabled(bool enabled) noexcept
{
	if (m_automaticEnabled == enabled) return;
	m_automaticEnabled = enabled;
	++m_generation;
}

void CWordMarker::SetAddedWordCharacters(std::wstring characters)
{
	characters.erase(std::remove_if(characters.begin(), characters.end(), [](wchar_t character) {
		return character == L'\0' || character == L'\r' || character == L'\n';
	}), characters.end());
	if (characters.size() > 64) characters.resize(64);
	if (m_addedWordCharacters == characters) return;
	m_addedWordCharacters = std::move(characters);
	++m_generation;
}

bool CWordMarker::IsWordCharacter(wchar_t character) const noexcept
{
	return IS_KEYWORD_CHAR(character) || m_addedWordCharacters.find(character) != std::wstring::npos;
}

bool CWordMarker::ExpandWordRange(const wchar_t* text, int length, int* from, int* to) const noexcept
{
	if (!text || !from || !to || m_addedWordCharacters.empty() || *from < 0 || *to < *from || *to > length) return false;
	const int originalFrom = *from;
	const int originalTo = *to;
	while (*from > 0 && IsWordCharacter(text[*from - 1])) --*from;
	while (*to < length && IsWordCharacter(text[*to])) ++*to;
	return *from != originalFrom || *to != originalTo;
}

const std::wstring* CWordMarker::AutomaticWord(const void* document) const noexcept
{
	const auto found = m_automaticByDocument.find(document);
	return found == m_automaticByDocument.end() ? nullptr : &found->second;
}

int CWordMarker::Match(const void* document, const wchar_t* text, int length, int position, int* end) const noexcept
{
	if (!text || !end || position < 0 || position >= length) return 0;
	int bestIndex = 0;
	size_t bestLength = 0;
	auto considerCandidates = [&](const std::vector<size_t>* candidates) {
		if (!candidates) return;
		for (const size_t index : *candidates) {
			if (!m_entries[index].enabled) continue;
			const auto& word = m_entries[index].word;
			if (word.size() > bestLength && word.size() <= static_cast<size_t>(length - position) &&
				(position == 0 || !IsWordCharacter(text[position - 1])) &&
				(position + static_cast<int>(word.size()) >= length || !IsWordCharacter(text[position + word.size()])) &&
				wmemcmp(text + position, word.data(), word.size()) == 0) {
				bestLength = word.size();
				bestIndex = static_cast<int>(index + 1);
			}
		}
	};
	const auto candidates = m_singleCharacterIndex.find(text[position]);
	considerCandidates(candidates == m_singleCharacterIndex.end() ? nullptr : &candidates->second);
	if (m_cachedAutomaticDocument != document) {
		const auto automatic = m_automaticByDocument.find(document);
		m_cachedAutomaticDocument = document;
		m_cachedAutomatic = automatic == m_automaticByDocument.end() ? nullptr : &automatic->second;
	}
	if (m_automaticEnabled && m_cachedAutomatic && !m_cachedAutomatic->empty() &&
		m_cachedAutomatic->size() > bestLength && m_cachedAutomatic->size() <= static_cast<size_t>(length - position) &&
		text[position] == m_cachedAutomatic->front() &&
		wmemcmp(text + position, m_cachedAutomatic->data(), m_cachedAutomatic->size()) == 0) {
		bestLength = m_cachedAutomatic->size();
		bestIndex = 33; // reserved automatic marker slot
	}
	if (bestIndex != 0) *end = position + static_cast<int>(bestLength);
	return bestIndex;
}

unsigned CWordMarker::ColorAt(int index) const noexcept
{
	return index > 0 && static_cast<size_t>(index) <= m_entries.size()
		? m_entries[index - 1].color : 0u;
}

void CWordMarker::Load(const std::vector<Entry>& entries)
{
	m_entries.clear();
	for (const auto& entry : entries) {
		if (m_entries.size() >= 32) break;
		if (entry.word.empty() || entry.word.size() > static_cast<size_t>(INT_MAX) ||
			entry.word.find(L'\0') != std::wstring::npos || entry.word.find(L'\r') != std::wstring::npos ||
			entry.word.find(L'\n') != std::wstring::npos) continue;
		if (std::any_of(m_entries.begin(), m_entries.end(), [&](const Entry& current) { return current.word == entry.word; })) continue;
		Entry loaded = entry;
		if (loaded.color == 0) loaded.color = NextColor({}, {});
		if (std::any_of(m_entries.begin(), m_entries.end(), [&](const Entry& current) { return current.color == loaded.color; })) {
			loaded.color = NextColor({}, {});
		}
		m_entries.push_back(std::move(loaded));
	}
	RebuildIndex();
	m_automaticByDocument.clear();
	m_cachedAutomaticDocument = nullptr;
	m_cachedAutomatic = nullptr;
	++m_generation;
}
