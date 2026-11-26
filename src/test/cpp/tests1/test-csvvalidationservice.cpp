#include <gtest/gtest.h>

#include "parse/CSVTableModel.h"
#include "parse/CSVValidationService.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

TEST(CSVValidationService, ReturnsVersionTaggedDiagnosticsAsynchronously)
{
	auto snapshot = csvtable::CSVTableSnapshot::Create(L"a,b\n1,2,3\n", {}, 73);
	ASSERT_NE(snapshot, nullptr);

	std::mutex mutex;
	std::condition_variable ready;
	std::optional<csvtable::ValidationResult> result;
	csvtable::CSVValidationService service;
	service.Schedule(29, snapshot, {}, [&](csvtable::ValidationResult completed) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			result = std::move(completed);
		}
		ready.notify_one();
	});

	std::unique_lock<std::mutex> lock(mutex);
	ASSERT_TRUE(ready.wait_for(lock, std::chrono::seconds(5), [&] { return result.has_value(); }));
	EXPECT_EQ(result->documentId, 29u);
	EXPECT_EQ(result->documentVersion, 73u);
	EXPECT_FALSE(result->failed);
	ASSERT_EQ(result->diagnostics.size(), 1u);
	EXPECT_EQ(result->diagnostics[0].code, L"CSV010");
}
