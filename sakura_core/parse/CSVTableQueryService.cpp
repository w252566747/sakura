/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "CSVTableQueryService.h"

#include <utility>

namespace csvtable {
CSVTableQueryService::CSVTableQueryService()
	: m_worker(&CSVTableQueryService::Run, this)
{
}

CSVTableQueryService::~CSVTableQueryService()
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

void CSVTableQueryService::Schedule(std::uint64_t documentId, std::uint64_t documentVersion,
	std::shared_ptr<const CSVTableSnapshot> snapshot, bool hasHeader,
	std::vector<Filter> filters, FilterLogic logic, std::vector<SortKey> sortKeys,
	Completion completion)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_stopping) return;
		++m_generation;
		if (m_activeCancellation) m_activeCancellation->store(true, std::memory_order_relaxed);
		m_pending = Request{
			documentId, documentVersion, m_generation, std::move(snapshot), hasHeader,
			std::move(filters), logic, std::move(sortKeys), std::move(completion)
		};
	}
	m_changed.notify_all();
}

void CSVTableQueryService::Cancel()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_generation;
		m_pending.reset();
		if (m_activeCancellation) m_activeCancellation->store(true, std::memory_order_relaxed);
	}
	m_changed.notify_all();
}

void CSVTableQueryService::Run()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	while (!m_stopping) {
		m_changed.wait(lock, [this] { return m_stopping || m_pending.has_value(); });
		if (m_stopping) break;

		Request request = std::move(*m_pending);
		m_pending.reset();
		std::shared_ptr<std::atomic_bool> cancellation;
		try {
			cancellation = std::make_shared<std::atomic_bool>(false);
		} catch (...) {
			TableQueryResult result;
			result.documentId = request.documentId;
			result.documentVersion = request.documentVersion;
			result.failed = true;
			const bool publish = !m_stopping && request.generation == m_generation;
			auto completion = std::move(request.completion);
			lock.unlock();
			if (publish && completion) {
				try { completion(std::move(result)); } catch (...) {}
			}
			lock.lock();
			continue;
		}
		m_activeCancellation = cancellation;
		lock.unlock();

		TableQueryResult result;
		result.documentId = request.documentId;
		result.documentVersion = request.documentVersion;
		try {
			CSVTableModel model(std::move(request.snapshot), request.hasHeader);
			result.viewOrder = model.BuildViewOrder(request.filters, request.logic, request.sortKeys,
				[cancellation] { return cancellation->load(std::memory_order_relaxed); });
		} catch (...) {
			result.failed = true;
		}

		const bool cancelled = cancellation->load(std::memory_order_relaxed) || result.viewOrder.cancelled;
		lock.lock();
		const bool publish = !m_stopping && !cancelled && request.generation == m_generation;
		if (m_activeCancellation == cancellation) m_activeCancellation.reset();
		lock.unlock();
		if (publish && request.completion) {
			try {
				request.completion(std::move(result));
			} catch (...) {
				// Keep callback failures from terminating the editor's worker thread.
			}
		}
		lock.lock();
	}
}
}
