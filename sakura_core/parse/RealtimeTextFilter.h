/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_REALTIMETEXTFILTER_H_
#define SAKURA_REALTIMETEXTFILTER_H_
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

class CBregexp;

namespace textfilter {
	enum class Logic { And, Or };

	struct Options {
		std::vector<std::wstring> includeTerms;
		std::vector<std::wstring> excludeTerms;
		Logic logic = Logic::And;
		bool caseSensitive = false;
		bool regularExpression = false;
	std::wstring regexDllPath;
		size_t contextRows = 0;
	};

	struct Line {
		size_t number = 0;
		std::wstring text;
	};

	class FilterError final {
	public:
		explicit FilterError(std::wstring message) : m_message(std::move(message)) {}
		const std::wstring& Message() const noexcept { return m_message; }
	private:
		std::wstring m_message;
	};

	struct Range {
		size_t first = 0;
		size_t last = 0; // inclusive
		size_t firstVisibleIndex = 0;
	};

	struct BatchResult {
		uint64_t requestToken = 0;
		uint64_t documentVersion = 0;
		size_t scannedLines = 0;
		std::vector<size_t> matchingLines;
		bool finalBatch = false;
		bool failed = false;
		std::wstring error;
	};

	class Matcher final {
	public:
		explicit Matcher(const Options& options);
		~Matcher();
		Matcher(Matcher&&) noexcept;
		Matcher& operator=(Matcher&&) noexcept;
		Matcher(const Matcher&) = delete;
		Matcher& operator=(const Matcher&) = delete;
		bool Matches(const std::wstring& line) const;

	private:
		struct Term;
		bool MatchAny(const std::vector<std::unique_ptr<Term>>& terms, const std::wstring& line) const;
		std::vector<std::unique_ptr<Term>> m_include;
		std::vector<std::unique_ptr<Term>> m_exclude;
		Logic m_logic;
		bool m_caseSensitive;
		bool m_regex;
	};

	std::vector<Range> BuildVisibleRanges(const std::vector<size_t>& matchingLines,
		size_t totalLines, size_t contextRows);
	size_t CountVisibleLines(const std::vector<Range>& ranges) noexcept;
	std::optional<size_t> SourceLineForVisibleIndex(const std::vector<Range>& ranges,
		size_t visibleIndex) noexcept;
	uint64_t FingerprintLine(std::wstring_view line, bool hasLineEnding = false) noexcept;

	class Service final {
	public:
		using Completion = std::function<void(BatchResult)>;
		Service();
		~Service();
		Service(const Service&) = delete;
		Service& operator=(const Service&) = delete;

		void Begin(uint64_t requestToken, uint64_t documentVersion, Options options, Completion completion);
		bool Submit(uint64_t requestToken, std::vector<Line>&& lines, bool finalBatch);
		void Cancel();

	private:
		struct Batch {
			uint64_t generation = 0;
			uint64_t requestToken = 0;
			uint64_t documentVersion = 0;
			std::vector<Line> lines;
			bool finalBatch = false;
		};
		void Run();

		std::mutex m_mutex;
		std::condition_variable m_changed;
		std::optional<Batch> m_pending;
		std::shared_ptr<std::atomic_bool> m_activeCancellation;
		Options m_options;
		Completion m_completion;
		uint64_t m_generation = 0;
		uint64_t m_requestToken = 0;
		uint64_t m_documentVersion = 0;
		bool m_busy = false;
		bool m_stopping = false;
		std::thread m_worker;
	};
}

#endif // SAKURA_REALTIMETEXTFILTER_H_
