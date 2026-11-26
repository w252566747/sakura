/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CSVVALIDATIONSERVICE_H_
#define SAKURA_CSVVALIDATIONSERVICE_H_
#pragma once

#include "CSVTableModel.h"
#include "CSVValidator.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace csvtable {
	struct ValidationResult {
		std::uint64_t documentId = 0;
		std::uint64_t documentVersion = 0;
		std::vector<Diagnostic> diagnostics;
		bool failed = false;
	};

	class CSVValidationService final {
	public:
		using Completion = std::function<void(ValidationResult)>;
		CSVValidationService();
		~CSVValidationService();
		CSVValidationService(const CSVValidationService&) = delete;
		CSVValidationService& operator=(const CSVValidationService&) = delete;
		void Schedule(std::uint64_t documentId, std::shared_ptr<const CSVTableSnapshot> snapshot,
			ValidationOptions options, Completion completion);
		void Cancel();
	private:
		struct Request {
			std::uint64_t documentId;
			std::uint64_t documentVersion;
			std::uint64_t generation;
			std::shared_ptr<const CSVTableSnapshot> snapshot;
			ValidationOptions options;
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

#endif // SAKURA_CSVVALIDATIONSERVICE_H_
