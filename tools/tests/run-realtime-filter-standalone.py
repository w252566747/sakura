#!/usr/bin/env python3
"""Compile and exercise the production real-time filter service on non-Windows hosts.

The test substitutes only Sakura's BREGEXP DLL adapter. Matching, range mapping,
batching, generation cancellation, and result tagging come from production code.
"""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="sakura-realtime-filter-") as temp:
        temp_path = Path(temp)
        (temp_path / "StdAfx.h").write_text("#pragma once\n", encoding="utf-8")
        (temp_path / "extmodule").mkdir()
        (temp_path / "extmodule/CBregexp.h").write_text(r'''#pragma once
#include <cwchar>
#include <regex>
#include <string>
using LPCWSTR = const wchar_t*;
constexpr int DLL_SUCCESS = 0;
class CBregexp {
public:
  enum Option { optNothing = 0, optCaseSensitive = 1 };
  int InitDll(const wchar_t*) { return DLL_SUCCESS; }
  bool Compile(const wchar_t* pattern, int flags) {
    try {
      auto options = std::regex_constants::ECMAScript;
      if (!(flags & optCaseSensitive)) options |= std::regex_constants::icase;
      expression = std::wregex(pattern, options);
      valid = true;
      error.clear();
      return true;
    } catch (const std::regex_error&) {
      valid = false;
      error = L"invalid regex";
      return false;
    }
  }
  bool Match(const wchar_t* text, int length) const {
    return valid && std::regex_search(text, text + length, expression);
  }
  const wchar_t* GetLastMessage() const { return error.c_str(); }
private:
  std::wregex expression;
  bool valid = false;
  std::wstring error;
};
''', encoding="utf-8")
        (temp_path / "main.cpp").write_text(r'''#include "RealtimeTextFilter.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <atomic>
#include <mutex>
#include <thread>

int main() {
  using namespace textfilter;
  Options options;
  options.includeTerms = {L"error", L"disk"};
  options.excludeTerms = {L"ignore"};
  Matcher matcher(options);
  assert(matcher.Matches(L"DISK ERROR"));
  assert(!matcher.Matches(L"disk only"));
  assert(!matcher.Matches(L"disk error ignore"));
  options.logic = Logic::Or;
  Matcher orMatcher(options);
  assert(orMatcher.Matches(L"disk only"));
  assert(!orMatcher.Matches(L"disk only ignore"));
  options.caseSensitive = true;
  Matcher caseSensitive(options);
  assert(!caseSensitive.Matches(L"DISK only"));
  assert(caseSensitive.Matches(L"disk only"));

  options = {};
  options.includeTerms = {L"^WARN", L"[0-9]+$"};
  options.regularExpression = true;
  Matcher regexMatcher(options);
  assert(regexMatcher.Matches(L"WARN 42"));
  assert(!regexMatcher.Matches(L"INFO 42"));
  options.includeTerms = {L"["};
  bool invalidPatternRejected = false;
  try { Matcher invalid(options); } catch (const FilterError&) { invalidPatternRejected = true; }
  assert(invalidPatternRejected);

  const auto ranges = BuildVisibleRanges({1, 4, 9}, 10, 1);
  assert(ranges.size() == 2);
  assert(CountVisibleLines(ranges) == 8);
  assert(SourceLineForVisibleIndex(ranges, 6) == 8);
  assert(!SourceLineForVisibleIndex(ranges, 8));
  assert(FingerprintLine(L"tail", false) != FingerprintLine(L"tail", true));

  std::mutex mutex;
  std::condition_variable ready;
  std::optional<BatchResult> result;
  Service service;
  Options serviceOptions;
  serviceOptions.includeTerms = {L"match"};
  service.Begin(17, 41, std::move(serviceOptions), [&](BatchResult value) {
    { std::lock_guard<std::mutex> lock(mutex); result = std::move(value); }
    ready.notify_one();
  });
  assert(service.Submit(17, {{0, L"no"}, {1, L"match one"}, {2, L"match two"}}, true));
  {
    std::unique_lock<std::mutex> lock(mutex);
    assert(ready.wait_for(lock, std::chrono::seconds(5), [&] { return result.has_value(); }));
  }
  assert(result->requestToken == 17 && result->documentVersion == 41);
  assert(result->finalBatch && !result->failed && result->scannedLines == 3);
  assert((result->matchingLines == std::vector<std::size_t>{1, 2}));

  std::atomic_bool cancelledResultPublished{false};
  service.Begin(18, 42, Options{}, [&](BatchResult) {
    cancelledResultPublished.store(true, std::memory_order_relaxed);
  });
  std::vector<Line> largeBatch;
  largeBatch.reserve(500000);
  for (std::size_t i = 0; i < 500000; ++i) largeBatch.push_back({i, L"ordinary log line"});
  assert(service.Submit(18, std::move(largeBatch), true));
  service.Cancel();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  assert(!cancelledResultPublished.load(std::memory_order_relaxed));
  std::cout << "production filter matcher, regex adapter, ranges and async service passed\n";
}
''', encoding="utf-8")
        executable = temp_path / "realtime-filter-test"
        subprocess.run([
            "clang++", "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=undefined", "-I", str(temp_path), "-I", str(ROOT / "sakura_core/parse"),
            str(ROOT / "sakura_core/parse/RealtimeTextFilter.cpp"), str(temp_path / "main.cpp"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
