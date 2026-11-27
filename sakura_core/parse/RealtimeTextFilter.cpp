/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "RealtimeTextFilter.h"
#include "extmodule/CBregexp.h"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <utility>

namespace textfilter {
namespace {
std::wstring FoldCase(const std::wstring& value)
{
	std::wstring folded;
	folded.reserve(value.size());
	for (const wchar_t ch : value) folded.push_back(static_cast<wchar_t>(std::towlower(ch)));
	return folded;
}

}

struct Matcher::Term {
	std::wstring literal;
	std::unique_ptr<CBregexp> regex;
};

Matcher::Matcher(const Options& options)
	: m_logic(options.logic)
	, m_caseSensitive(options.caseSensitive)
	, m_regex(options.regularExpression)
{
	auto compileTerms = [this, &options](const std::vector<std::wstring>& source,
		std::vector<std::unique_ptr<Term>>& destination) {
		destination.reserve(source.size());
		for (const auto& value : source) {
			if (value.empty()) continue;
			auto term = std::make_unique<Term>();
			term->literal = m_caseSensitive || m_regex ? value : FoldCase(value);
			if (m_regex) {
				term->regex = std::make_unique<CBregexp>();
				LPCWSTR dllPath = options.regexDllPath.empty() ? nullptr : options.regexDllPath.c_str();
				if (term->regex->InitDll(dllPath) != DLL_SUCCESS) {
					throw FilterError(L"正規表現ライブラリを読み込めません。共通設定の正規表現ライブラリを確認してください。");
				}
				const int flags = m_caseSensitive ? CBregexp::optCaseSensitive : CBregexp::optNothing;
				if (!term->regex->Compile(value.c_str(), flags)) {
					const wchar_t* message = term->regex->GetLastMessage();
					throw FilterError(message && message[0] ? message : L"正規表現をコンパイルできませんでした。");
				}
			}
			destination.push_back(std::move(term));
		}
	};
	compileTerms(options.includeTerms, m_include);
	compileTerms(options.excludeTerms, m_exclude);
}

Matcher::~Matcher() = default;
Matcher::Matcher(Matcher&&) noexcept = default;
Matcher& Matcher::operator=(Matcher&&) noexcept = default;

bool Matcher::Matches(const std::wstring& line) const
{
	if (m_regex) {
		if (line.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
		auto matchesAny = [&line](const std::vector<std::unique_ptr<Term>>& terms) {
			return std::any_of(terms.begin(), terms.end(), [&line](const std::unique_ptr<Term>& term) {
				return term->regex && term->regex->Match(line.c_str(), static_cast<int>(line.size()));
			});
		};
		if (matchesAny(m_exclude)) return false;
		if (m_include.empty()) return true;
		if (m_logic == Logic::Or) return matchesAny(m_include);
		return std::all_of(m_include.begin(), m_include.end(), [&line](const std::unique_ptr<Term>& term) {
			return term->regex && term->regex->Match(line.c_str(), static_cast<int>(line.size()));
		});
	}
	std::wstring folded;
	std::wstring_view haystack(line);
	if (!m_caseSensitive) {
		folded = FoldCase(line);
		haystack = folded;
	}
	auto matchesAny = [haystack](const std::vector<std::unique_ptr<Term>>& terms) {
		return std::any_of(terms.begin(), terms.end(), [haystack](const std::unique_ptr<Term>& term) {
			return haystack.find(term->literal) != std::wstring_view::npos;
		});
	};
	if (matchesAny(m_exclude)) return false;
	if (m_include.empty()) return true;
	if (m_logic == Logic::Or) return matchesAny(m_include);
	return std::all_of(m_include.begin(), m_include.end(), [haystack](const std::unique_ptr<Term>& term) {
		return haystack.find(term->literal) != std::wstring_view::npos;
	});
}

std::vector<Range> BuildVisibleRanges(const std::vector<size_t>& matchingLines,
	size_t totalLines, size_t contextRows)
{
	std::vector<Range> ranges;
	if (totalLines == 0 || matchingLines.empty()) return ranges;
	size_t visibleCount = 0;
	for (const size_t match : matchingLines) {
		if (match >= totalLines) continue;
		const size_t first = match > contextRows ? match - contextRows : 0;
		const size_t last = contextRows > totalLines - 1 - match ? totalLines - 1 : match + contextRows;
		if (!ranges.empty() && (first <= ranges.back().last ||
			(ranges.back().last != static_cast<size_t>(-1) && first == ranges.back().last + 1))) {
			if (last > ranges.back().last) visibleCount += last - ranges.back().last;
			ranges.back().last = std::max(ranges.back().last, last);
		} else {
			ranges.push_back({ first, last, visibleCount });
			visibleCount += last - first + 1;
		}
	}
	return ranges;
}

size_t CountVisibleLines(const std::vector<Range>& ranges) noexcept
{
	size_t count = 0;
	for (const auto& range : ranges) {
		if (range.last >= range.first) count += range.last - range.first + 1;
	}
	return count;
}

std::optional<size_t> SourceLineForVisibleIndex(const std::vector<Range>& ranges,
	size_t visibleIndex) noexcept
{
	const auto range = std::lower_bound(ranges.begin(), ranges.end(), visibleIndex,
		[](const Range& candidate, size_t index) {
			const size_t length = candidate.last - candidate.first + 1;
			return candidate.firstVisibleIndex + length <= index;
		});
	if (range == ranges.end() || visibleIndex < range->firstVisibleIndex) return std::nullopt;
	const size_t offset = visibleIndex - range->firstVisibleIndex;
	if (offset > range->last - range->first) return std::nullopt;
	return range->first + offset;
}

uint64_t FingerprintLine(std::wstring_view line, bool hasLineEnding) noexcept
{
	// FNV-1a over each decoded wchar_t value. The fingerprint is kept only for
	// the current process and is used to confirm that an external reload only
	// appended lines before reusing prior filter results.
	uint64_t hash = 14695981039346656037ull;
	for (const wchar_t ch : line) {
		const uint32_t value = static_cast<uint32_t>(ch);
		for (unsigned int byte = 0; byte < sizeof(value); ++byte) {
			hash ^= static_cast<uint8_t>(value >> (byte * 8));
			hash *= 1099511628211ull;
		}
	}
	hash ^= static_cast<uint64_t>(line.size());
	hash *= 1099511628211ull;
	hash ^= static_cast<uint64_t>(hasLineEnding);
	hash *= 1099511628211ull;
	return hash;
}

Service::Service()
	: m_worker(&Service::Run, this)
{
}

Service::~Service()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stopping = true;
		++m_generation;
		m_pending.reset();
		if (m_activeCancellation) m_activeCancellation->store(true, std::memory_order_relaxed);
	}
	m_changed.notify_all();
	if (m_worker.joinable()) m_worker.join();
}

void Service::Begin(uint64_t requestToken, uint64_t documentVersion, Options options, Completion completion)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_stopping) return;
	++m_generation;
	if (m_activeCancellation) m_activeCancellation->store(true, std::memory_order_relaxed);
	m_pending.reset();
	m_activeCancellation = std::make_shared<std::atomic_bool>(false);
	m_requestToken = requestToken;
	m_documentVersion = documentVersion;
	m_options = std::move(options);
	m_completion = std::move(completion);
}

bool Service::Submit(uint64_t requestToken, std::vector<Line>&& lines, bool finalBatch)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_stopping || requestToken != m_requestToken || m_pending || m_busy) return false;
		m_pending = Batch{ m_generation, requestToken, m_documentVersion, std::move(lines), finalBatch };
	}
	m_changed.notify_one();
	return true;
}

void Service::Cancel()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_generation;
		m_requestToken = 0;
		m_pending.reset();
		if (m_activeCancellation) m_activeCancellation->store(true, std::memory_order_relaxed);
	}
	m_changed.notify_all();
}

void Service::Run()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	uint64_t compiledGeneration = 0;
	std::unique_ptr<Matcher> matcher;
	while (!m_stopping) {
		m_changed.wait(lock, [this] { return m_stopping || m_pending.has_value(); });
		if (m_stopping) break;
		Batch batch = std::move(*m_pending);
		m_pending.reset();
		m_busy = true;
		const Options options = m_options;
		const Completion completion = m_completion;
		const auto cancellation = m_activeCancellation;
		lock.unlock();

		BatchResult result;
		result.requestToken = batch.requestToken;
		result.documentVersion = batch.documentVersion;
		result.finalBatch = batch.finalBatch;
		try {
			if (compiledGeneration != batch.generation || !matcher) {
				matcher = std::make_unique<Matcher>(options);
				compiledGeneration = batch.generation;
			}
			for (auto& line : batch.lines) {
				if (cancellation && cancellation->load(std::memory_order_relaxed)) break;
				if (matcher->Matches(line.text)) result.matchingLines.push_back(line.number);
				++result.scannedLines;
			}
		} catch (const FilterError& error) {
			result.failed = true;
			result.finalBatch = true;
			result.error = error.Message();
			matcher.reset();
			compiledGeneration = 0;
		} catch (...) {
			result.failed = true;
			result.finalBatch = true;
			result.error = L"フィルター処理中にメモリ不足または内部エラーが発生しました。";
		}

		lock.lock();
		const bool publish = !m_stopping && batch.generation == m_generation &&
			!(cancellation && cancellation->load(std::memory_order_relaxed));
		m_busy = false;
		lock.unlock();
		if (publish && completion) {
			try { completion(std::move(result)); } catch (...) {}
		}
		lock.lock();
	}
}
}
