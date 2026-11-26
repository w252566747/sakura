/*
	Copyright (C) 2026 Sakura Editor Organization
	Custom enhancement based on Sakura Editor.
	SPDX-License-Identifier: Zlib
*/
#ifndef SAKURA_CSVTABLEMODEL_H_
#define SAKURA_CSVTABLEMODEL_H_
#pragma once

#include "CSVParser.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace csvtable {
	class CSVTableSnapshot final {
	public:
		static std::shared_ptr<const CSVTableSnapshot> Create(
			std::wstring text,
			ParseOptions options = {},
			std::uint64_t documentVersion = 0,
			const std::function<bool()>& isCancelled = {});

		std::wstring_view Text() const noexcept { return m_text; }
		const ParseResult& Parsed() const noexcept { return m_parsed; }
		ParseOptions Options() const noexcept { return m_options; }
		std::uint64_t DocumentVersion() const noexcept { return m_documentVersion; }

	private:
		CSVTableSnapshot(std::wstring text, ParseOptions options, std::uint64_t documentVersion,
			const std::function<bool()>& isCancelled);

		std::wstring m_text;
		ParseOptions m_options;
		std::uint64_t m_documentVersion;
		ParseResult m_parsed;
	};

	struct RawFieldRange {
		std::size_t begin = 0;
		std::size_t length = 0;
		bool valid = false;
	};

	struct TextEditPatch {
		std::size_t begin = 0;
		std::size_t deleteLength = 0;
		std::wstring replacement;
		bool valid = false;
	std::uint64_t documentVersion = 0;
	};

	using TextEditBatch = std::vector<TextEditPatch>;

	enum class FilterOperator {
		Contains,
		NotContains,
		Equals,
		StartsWith,
		EndsWith,
		Regex,
		NumberGreater,
		NumberLess,
		NumberEqual,
		Empty,
		NonEmpty,
	};

	enum class FilterLogic { And, Or };
	enum class SortKind { String, Numeric, Date, Natural };

	struct Filter {
		std::size_t column = 0;
		FilterOperator op = FilterOperator::Contains;
		std::wstring value;
		bool caseSensitive = false;
	};

	struct SortKey {
		std::size_t column = 0;
		SortKind kind = SortKind::String;
		bool descending = false;
	};

	struct ViewOrderResult {
		std::vector<std::size_t> rows; // Data-row indexes; empty with identity=true means source order.
		bool identity = true;
		bool cancelled = false;
		std::size_t invalidFilter = static_cast<std::size_t>(-1);
	};

	class CSVTableModel final {
	public:
		CSVTableModel(std::shared_ptr<const CSVTableSnapshot> snapshot, bool hasHeader);

		std::size_t RowCount() const noexcept;
		std::size_t ColumnCount() const noexcept;
		bool HasHeader() const noexcept { return m_hasHeader; }
		std::uint64_t DocumentVersion() const noexcept;
		std::size_t RecordIndexForRow(std::size_t dataRow) const noexcept;
		std::wstring ColumnTitle(std::size_t column) const;
		std::wstring CellText(std::size_t dataRow, std::size_t column) const;
		RawFieldRange CellRawRange(std::size_t dataRow, std::size_t column) const noexcept;
		TextEditPatch PrepareCellEdit(std::size_t dataRow, std::size_t column, std::wstring_view value) const;
		TextEditPatch PrepareAppendRow(const std::vector<std::wstring>& values) const;
		TextEditPatch PrepareDeleteRow(std::size_t dataRow) const noexcept;
		TextEditBatch PrepareAppendColumn() const;
		TextEditBatch PrepareDeleteColumn(std::size_t column) const;
		std::size_t FirstPhysicalLine(std::size_t dataRow) const noexcept;
		// Call on a worker for large inputs. It only returns display-row indexes and never mutates source text.
		ViewOrderResult BuildViewOrder(
			const std::vector<Filter>& filters,
			FilterLogic logic,
			const std::vector<SortKey>& sortKeys,
			const std::function<bool()>& isCancelled = {}) const;
		const CSVTableSnapshot* Snapshot() const noexcept { return m_snapshot.get(); }

	private:
		const Record* RecordAtDataRow(std::size_t dataRow) const noexcept;
		std::shared_ptr<const CSVTableSnapshot> m_snapshot;
		bool m_hasHeader;
	};
}

#endif // SAKURA_CSVTABLEMODEL_H_
