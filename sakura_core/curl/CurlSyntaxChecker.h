/*!
	@file CurlSyntaxChecker.h
	@brief Conservative lexical checks for Curl 8.0 source.
	SPDX-License-Identifier: Zlib
*/
#pragma once

#include <cstddef>
#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace curlsyntax {

enum class Severity { Error, Warning };

struct Diagnostic {
	Severity severity = Severity::Error;
	std::string code;
	std::wstring message;
	size_t offset = 0;
	int line = 1;
	int column = 1;
};

// Checks conservative lexical conditions and comma errors for known built-in
// box containers; comma inference is suppressed inside user-defined macros.
std::vector<Diagnostic> Check(std::wstring_view source, const std::atomic_bool* canceled = nullptr);

} // namespace curlsyntax
