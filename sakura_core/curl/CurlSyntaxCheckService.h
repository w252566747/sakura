/*!
	@file CurlSyntaxCheckService.h
	@brief Debounced background Curl syntax check queue.
	SPDX-License-Identifier: Zlib
*/
#pragma once

#include "curl/CurlSyntaxChecker.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace curlsyntax {

bool IsRealtimeEnabled() noexcept;
bool IsRealtimePreferenceEnabled() noexcept;
void SetRealtimeEnabled(bool enabled) noexcept;
void SetFeatureEnabled(bool enabled) noexcept;
int GetErrorListHeight() noexcept;
void SetErrorListHeight(int height) noexcept;

struct CheckResult {
	uint64_t documentId = 0;
	uint64_t version = 0;
	bool manual = false;
	std::vector<Diagnostic> diagnostics;
};

class CheckService final {
public:
	using Completion = std::function<void(CheckResult)>;

	explicit CheckService(std::chrono::milliseconds debounce = std::chrono::milliseconds(500));
	~CheckService();
	CheckService(const CheckService&) = delete;
	CheckService& operator=(const CheckService&) = delete;

	// Replaces any queued or running request. Completion runs on the worker
	// thread and must only enqueue a result for later UI-thread processing.
	void Schedule(uint64_t documentId, uint64_t version, std::wstring snapshot, Completion completion, bool immediate = false, bool manual = false);
	void Cancel();

private:
	struct Request {
		uint64_t documentId;
		uint64_t version;
		uint64_t generation;
		bool manual;
		std::wstring snapshot;
		Completion completion;
		std::chrono::steady_clock::time_point due;
	};

	void Run();

	const std::chrono::milliseconds m_debounce;
	std::mutex m_mutex;
	std::condition_variable m_changed;
	std::optional<Request> m_pending;
	std::shared_ptr<std::atomic_bool> m_activeCancellation;
	uint64_t m_generation = 0;
	bool m_stopping = false;
	std::thread m_worker;
};

} // namespace curlsyntax
