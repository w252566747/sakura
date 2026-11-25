/*!
	@file CurlSyntaxChecker.h
	@brief Conservative lexical checks and likely built-in name typo warnings.
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

// Checks conservative lexical conditions, likely spelling mistakes against the
// bundled Curl keyword dictionary, and comma errors in known box containers.
// Unknown user-defined names remain allowed; spelling diagnostics are warnings.
std::vector<Diagnostic> Check(std::wstring_view source, const std::atomic_bool* canceled = nullptr);

} // namespace curlsyntax
