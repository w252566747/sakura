/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "CWordMarker.h"
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
		return true;
	}
	if (m_entries.size() >= 32) return false;
	m_entries.push_back({ std::move(word), color });
	RebuildIndex();
	return true;
}

bool CWordMarker::SetColor(size_t index, unsigned color) noexcept
{
	if (index >= m_entries.size()) return false;
	m_entries[index].color = color;
	return true;
}

unsigned CWordMarker::NextColor(const std::vector<unsigned>& palette) const noexcept
{
	for (size_t index = 0; index < palette.size(); ++index) {
		const auto color = palette[index];
		if (std::find(palette.begin(), palette.begin() + index, color) != palette.begin() + index) continue;
		if (std::none_of(m_entries.begin(), m_entries.end(), [color](const Entry& entry) { return entry.color == color; })) return color;
	}
	return palette.empty() ? 0u : palette[m_entries.size() % palette.size()];
}

void CWordMarker::ClearFixed() noexcept
{
	m_entries.clear();
	m_singleCharacterIndex.clear();
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
}

void CWordMarker::ClearAutomatic(const void* document) noexcept
{
	m_automaticByDocument.erase(document);
	m_cachedAutomaticDocument = nullptr;
	m_cachedAutomatic = nullptr;
}

int CWordMarker::Match(const void* document, const wchar_t* text, int length, int position, int* end) const noexcept
{
	if (!text || !end || position < 0 || position >= length) return 0;
	int bestIndex = 0;
	size_t bestLength = 0;
	auto considerCandidates = [&](const std::vector<size_t>* candidates) {
		if (!candidates) return;
		for (const size_t index : *candidates) {
			const auto& word = m_entries[index].word;
			if (word.size() > bestLength && word.size() <= static_cast<size_t>(length - position) &&
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
		m_entries.push_back(entry);
	}
	RebuildIndex();
	m_automaticByDocument.clear();
	m_cachedAutomaticDocument = nullptr;
	m_cachedAutomatic = nullptr;
}
