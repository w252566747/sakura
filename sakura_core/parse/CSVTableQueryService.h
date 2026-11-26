/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CSVTABLEQUERYSERVICE_H_
#define SAKURA_CSVTABLEQUERYSERVICE_H_
#pragma once

#include "CSVTableModel.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace csvtable {
	struct TableQueryResult {
		std::uint64_t documentId = 0;
		std::uint64_t documentVersion = 0;
		std::uint64_t requestToken = 0;
		ViewOrderResult viewOrder;
		bool failed = false;
	};

	class CSVTableQueryService final {
	public:
		using Completion = std::function<void(TableQueryResult)>;

		CSVTableQueryService();
		~CSVTableQueryService();
		CSVTableQueryService(const CSVTableQueryService&) = delete;
		CSVTableQueryService& operator=(const CSVTableQueryService&) = delete;

		void Schedule(std::uint64_t documentId, std::uint64_t documentVersion,
			std::shared_ptr<const CSVTableSnapshot> snapshot, bool hasHeader,
			std::vector<Filter> filters, FilterLogic logic, std::vector<SortKey> sortKeys,
			Completion completion);
		void Cancel();

	private:
		struct Request {
			std::uint64_t documentId;
			std::uint64_t documentVersion;
			std::uint64_t generation;
			std::shared_ptr<const CSVTableSnapshot> snapshot;
			bool hasHeader;
			std::vector<Filter> filters;
			FilterLogic logic;
			std::vector<SortKey> sortKeys;
			Completion completion;
		};

		void Run();

		std::mutex m_mutex;
		std::condition_variable m_changed;
		std::optional<Request> m_pending;
		std::shared_ptr<std::atomic_bool> m_activeCancellation;
		std::uint64_t m_generation = 0;
		bool m_stopping = false;
		std::thread m_worker;
	};
}

#endif // SAKURA_CSVTABLEQUERYSERVICE_H_
