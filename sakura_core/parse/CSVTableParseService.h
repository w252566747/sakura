/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CSVTABLEPARSESERVICE_H_
#define SAKURA_CSVTABLEPARSESERVICE_H_
#pragma once

#include "CSVTableModel.h"

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace csvtable {
	struct TableParseResult {
		std::uint64_t documentId = 0;
		std::uint64_t documentVersion = 0;
		std::shared_ptr<const CSVTableSnapshot> snapshot;
		bool failed = false;
	};

	class CSVTableParseService final {
	public:
		using Completion = std::function<void(TableParseResult)>;

		explicit CSVTableParseService(std::chrono::milliseconds debounce = std::chrono::milliseconds(150));
		~CSVTableParseService();
		CSVTableParseService(const CSVTableParseService&) = delete;
		CSVTableParseService& operator=(const CSVTableParseService&) = delete;

		// Replaces any pending or running snapshot build. Completion runs on the worker thread.
		void Schedule(std::uint64_t documentId, std::uint64_t documentVersion,
			std::wstring text, ParseOptions options, Completion completion, bool immediate = false);
		void Cancel();

	private:
		struct Request {
			std::uint64_t documentId;
			std::uint64_t documentVersion;
			std::uint64_t generation;
			std::wstring text;
			ParseOptions options;
			Completion completion;
			std::chrono::steady_clock::time_point due;
		};

		void Run();

		const std::chrono::milliseconds m_debounce;
		std::mutex m_mutex;
		std::condition_variable m_changed;
		std::optional<Request> m_pending;
		std::shared_ptr<std::atomic_bool> m_activeCancellation;
		std::uint64_t m_generation = 0;
		bool m_stopping = false;
		std::thread m_worker;
	};
}

#endif // SAKURA_CSVTABLEPARSESERVICE_H_
