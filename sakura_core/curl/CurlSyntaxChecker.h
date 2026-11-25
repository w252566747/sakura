/*!
	@file CurlSyntaxChecker.h
	@brief Conservative lexical checks and expression-head typo warnings.
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

// Checks conservative lexical conditions, likely spelling mistakes in Curl
// expression heads, and comma errors in known layout containers. User-defined
// identifiers remain allowed; spelling diagnostics are warnings, not errors.
std::vector<Diagnostic> Check(std::wstring_view source, const std::atomic_bool* canceled = nullptr);

} // namespace curlsyntax
