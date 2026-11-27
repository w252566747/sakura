/*!
	@file CurlSyntaxCheckService.cpp
	@brief Debounced background Curl syntax check queue.
	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "curl/CurlSyntaxCheckService.h"

#include <algorithm>
#include <atomic>

namespace curlsyntax {
namespace {
std::atomic_bool g_realtimeEnabled = false;
std::atomic_bool g_featureEnabled = true;
std::atomic_int g_errorListHeight = 150;
}

bool IsRealtimeEnabled() noexcept
{
	return g_realtimeEnabled.load(std::memory_order_relaxed)
		&& g_featureEnabled.load(std::memory_order_relaxed);
}

bool IsRealtimePreferenceEnabled() noexcept
{
	return g_realtimeEnabled.load(std::memory_order_relaxed);
}

void SetRealtimeEnabled(bool enabled) noexcept
{
	g_realtimeEnabled.store(enabled, std::memory_order_relaxed);
}

void SetFeatureEnabled(bool enabled) noexcept
{
	g_featureEnabled.store(enabled, std::memory_order_relaxed);
}

int GetErrorListHeight() noexcept
{
	return g_errorListHeight.load(std::memory_order_relaxed);
}

void SetErrorListHeight(int height) noexcept
{
	g_errorListHeight.store(std::clamp(height, 80, 600), std::memory_order_relaxed);
}

CheckService::CheckService(std::chrono::milliseconds debounce)
	: m_debounce(debounce)
	, m_worker(&CheckService::Run, this)
{
}

CheckService::~CheckService()
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

void CheckService::Schedule(uint64_t documentId, uint64_t version,
	std::wstring snapshot, Completion completion, bool immediate, bool manual)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_stopping) return;
		++m_generation;
		if (m_activeCancellation) m_activeCancellation->store(true, std::memory_order_relaxed);
		m_pending = Request{
			documentId,
			version,
			m_generation,
			manual,
			std::move(snapshot),
			std::move(completion),
			std::chrono::steady_clock::now() + (immediate ? std::chrono::milliseconds(0) : m_debounce)
		};
	}
	m_changed.notify_all();
}

void CheckService::Cancel()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_generation;
		m_pending.reset();
		if (m_activeCancellation) m_activeCancellation->store(true, std::memory_order_relaxed);
	}
	m_changed.notify_all();
}

void CheckService::Run()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	while (!m_stopping) {
		m_changed.wait(lock, [this] { return m_stopping || m_pending.has_value(); });
		if (m_stopping) break;

		const uint64_t generation = m_pending->generation;
		const auto due = m_pending->due;
		if (m_changed.wait_until(lock, due, [this, generation] {
			return m_stopping || !m_pending || m_pending->generation != generation;
		})) {
			continue;
		}

		Request request = std::move(*m_pending);
		m_pending.reset();
		auto cancellation = std::make_shared<std::atomic_bool>(false);
		m_activeCancellation = cancellation;
		lock.unlock();

		auto diagnostics = Check(request.snapshot, cancellation.get());
		const bool canceled = cancellation->load(std::memory_order_relaxed);
		lock.lock();
		const bool publish = !m_stopping && !canceled && request.generation == m_generation;
		if (m_activeCancellation == cancellation) m_activeCancellation.reset();
		lock.unlock();

		if (publish && request.completion) {
			try {
			request.completion(CheckResult{ request.documentId, request.version, request.manual, std::move(diagnostics) });
			} catch (...) {
				// Keep callback failures from terminating the editor's worker thread.
			}
		}
		lock.lock();
	}
}

} // namespace curlsyntax
