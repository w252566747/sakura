#include <gtest/gtest.h>

#include "parse/CSVTableQueryService.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

TEST(CSVTableQueryService, ReturnsVersionTaggedNumericSortOrderOnWorker)
{
	auto snapshot = csvtable::CSVTableSnapshot::Create(L"value\n3\n1\n2", {}, 41);
	ASSERT_NE(snapshot, nullptr);

	std::mutex mutex;
	std::condition_variable ready;
	std::optional<csvtable::TableQueryResult> result;
	csvtable::CSVTableQueryService service;
	csvtable::SortKey key;
	key.column = 0;
	key.kind = csvtable::SortKind::Numeric;
	service.Schedule(17, 41, snapshot, true, {}, csvtable::FilterLogic::And, { key },
		[&](csvtable::TableQueryResult completed) {
			{
				std::lock_guard<std::mutex> lock(mutex);
				result = std::move(completed);
			}
			ready.notify_one();
		});

	std::unique_lock<std::mutex> lock(mutex);
	ASSERT_TRUE(ready.wait_for(lock, std::chrono::seconds(5), [&] { return result.has_value(); }));
	EXPECT_EQ(result->documentId, 17u);
	EXPECT_EQ(result->documentVersion, 41u);
	EXPECT_FALSE(result->failed);
	ASSERT_FALSE(result->viewOrder.identity);
	ASSERT_EQ(result->viewOrder.rows.size(), 3u);
	EXPECT_EQ(result->viewOrder.rows[0], 1u);
	EXPECT_EQ(result->viewOrder.rows[1], 2u);
	EXPECT_EQ(result->viewOrder.rows[2], 0u);
}
