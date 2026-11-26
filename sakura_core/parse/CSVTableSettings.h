/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CSVTABLESETTINGS_H_
#define SAKURA_CSVTABLESETTINGS_H_
#pragma once

#include <vector>

namespace csvtable {
	struct UserSettings {
		wchar_t delimiter = L',';
		bool autoDetectDelimiter = true;
		bool hasHeader = false;
		std::vector<int> columnWidths;
	};

	inline UserSettings& GetUserSettings() noexcept
	{
		static UserSettings settings;
		return settings;
	}
}

#endif // SAKURA_CSVTABLESETTINGS_H_
