/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "CSVTableParseService.h"

#include <atomic>
#include <utility>

namespace csvtable {
CSVTableParseService::CSVTableParseService(std::chrono::milliseconds debounce)
	: m_debounce(debounce)
	, m_worker(&CSVTableParseService::Run, this)
{
}

CSVTableParseService::~CSVTableParseService()
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

void CSVTableParseService::Schedule(std::uint64_t documentId, std::uint64_t documentVersion,
	std::wstring text, ParseOptions options, Completion completion, bool immediate)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_stopping) return;
		++m_generation;
		if (m_activeCancellation) m_activeCancellation->store(true, std::memory_order_relaxed);
		m_pending = Request{
			documentId,
			documentVersion,
			m_generation,
			std::move(text),
			options,
			std::move(completion),
			std::chrono::steady_clock::now() + (immediate ? std::chrono::milliseconds(0) : m_debounce)
		};
	}
	m_changed.notify_all();
}

void CSVTableParseService::Cancel()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_generation;
		m_pending.reset();
		if (m_activeCancellation) m_activeCancellation->store(true, std::memory_order_relaxed);
	}
	m_changed.notify_all();
}

void CSVTableParseService::Run()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	while (!m_stopping) {
		m_changed.wait(lock, [this] { return m_stopping || m_pending.has_value(); });
		if (m_stopping) break;
		const std::uint64_t generation = m_pending->generation;
		const auto due = m_pending->due;
		if (m_changed.wait_until(lock, due, [this, generation] {
			return m_stopping || !m_pending || m_pending->generation != generation;
		})) continue;

		Request request = std::move(*m_pending);
		m_pending.reset();
		auto cancellation = std::make_shared<std::atomic_bool>(false);
		m_activeCancellation = cancellation;
		lock.unlock();

		std::shared_ptr<const CSVTableSnapshot> snapshot;
		bool failed = false;
		try {
			snapshot = CSVTableSnapshot::Create(std::move(request.text), request.options,
				request.documentVersion, [cancellation] {
					return cancellation->load(std::memory_order_relaxed);
				});
		} catch (...) {
			// Allocation or parse failures should not terminate the editor's worker thread.
			failed = true;
		}
		const bool cancelled = cancellation->load(std::memory_order_relaxed);
		lock.lock();
		const bool publish = !m_stopping && !cancelled && (snapshot || failed) && request.generation == m_generation;
		if (m_activeCancellation == cancellation) m_activeCancellation.reset();
		lock.unlock();
		if (publish && request.completion) {
			try {
				request.completion({ request.documentId, request.documentVersion, std::move(snapshot), failed });
			} catch (...) {
				// Keep callback failures from terminating the editor's worker thread.
			}
		}
		lock.lock();
	}
}
}
