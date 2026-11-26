/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#include "StdAfx.h"
#include "CSVValidationService.h"

#include <utility>

namespace csvtable {
CSVValidationService::CSVValidationService()
	: m_worker(&CSVValidationService::Run, this)
{
}

CSVValidationService::~CSVValidationService()
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

void CSVValidationService::Schedule(std::uint64_t documentId,
	std::shared_ptr<const CSVTableSnapshot> snapshot, ValidationOptions options, Completion completion)
{
	if (!snapshot) return;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_stopping) return;
		++m_generation;
		if (m_activeCancellation) m_activeCancellation->store(true, std::memory_order_relaxed);
		m_pending = Request{ documentId, snapshot->DocumentVersion(), m_generation,
			std::move(snapshot), std::move(options), std::move(completion) };
	}
	m_changed.notify_all();
}

void CSVValidationService::Cancel()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_generation;
		m_pending.reset();
		if (m_activeCancellation) m_activeCancellation->store(true, std::memory_order_relaxed);
	}
	m_changed.notify_all();
}

void CSVValidationService::Run()
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
			ValidationResult result;
			result.documentId = request.documentId;
			result.documentVersion = request.documentVersion;
			result.failed = true;
			auto completion = std::move(request.completion);
			const bool publish = !m_stopping && request.generation == m_generation;
			lock.unlock();
			if (publish && completion) {
				try { completion(std::move(result)); } catch (...) {}
			}
			lock.lock();
			continue;
		}
		m_activeCancellation = cancellation;
		lock.unlock();

		ValidationResult result;
		result.documentId = request.documentId;
		result.documentVersion = request.documentVersion;
		try {
			result.diagnostics = Validate(request.snapshot->Text(), request.snapshot->Parsed(), request.options,
				[cancellation] { return cancellation->load(std::memory_order_relaxed); });
		} catch (...) {
			result.failed = true;
		}
		const bool cancelled = cancellation->load(std::memory_order_relaxed);
		lock.lock();
		const bool publish = !m_stopping && !cancelled && request.generation == m_generation;
		if (m_activeCancellation == cancellation) m_activeCancellation.reset();
		lock.unlock();
		if (publish && request.completion) {
			try { request.completion(std::move(result)); } catch (...) {}
		}
		lock.lock();
	}
}
}
