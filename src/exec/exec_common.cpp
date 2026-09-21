// SPDX-License-Identifier: GPL-2.0-or-later

#include "routing/exec_common.hpp"

#include <cstdlib>
#include <sstream>

#include "duckdb/common/exception.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/logging/logger.hpp"

#include "c_types/path_rt.h"
#include "drivers/shortestPath_driver.hpp"
#include "routing/driver_input.hpp"
#include "routing/input_registry.hpp"

namespace duckdb_routing {

DriverResult::~DriverResult() {
	if (rows) {
		std::free(rows);
	}
}

DriverResult::DriverResult(DriverResult &&other) noexcept
    : rows(other.rows), count(other.count), is_matrix(other.is_matrix) {
	other.rows = nullptr;
	other.count = 0;
}

DriverResult &DriverResult::operator=(DriverResult &&other) noexcept {
	if (this != &other) {
		if (rows) {
			std::free(rows);
		}
		rows = other.rows;
		count = other.count;
		is_matrix = other.is_matrix;
		other.rows = nullptr;
		other.count = 0;
	}
	return *this;
}

DriverResult RunShortestPath(duckdb::ClientContext &context, InputRegistry &registry, const DriverRequest &request) {
	DriverResult result;
	std::ostringstream log;
	std::ostringstream notice;
	std::ostringstream err;

	ScopedIntArray starts(request.starts);
	ScopedIntArray ends(request.ends);

	{
		ScopedRoutingContext scope(context, registry);
		do_shortestPath(request.edges_sql, request.points_sql, request.combinations_sql,
		                request.has_starts ? starts.get() : nullptr, request.has_ends ? ends.get() : nullptr,
		                request.directed, request.only_cost, request.normal, request.n_goals, request.global,
		                request.driving_side, request.details, request.which, result.is_matrix, result.rows,
		                result.count, log, notice, err);

		// The driver catches every exception, so an interrupt only shows up as a flag.
		if (WasInterrupted()) {
			ClearInterrupted();
			throw duckdb::InterruptException();
		}
	}

	const auto err_text = err.str();
	if (!err_text.empty()) {
		// Upstream's process layer drops partial results when err is set; so do we.
		result = DriverResult();
		const auto hint = log.str();
		// A prefix match, not a substring one: this is the only arm that raises
		// InternalException, which invalidates the whole database instance, and err text can carry
		// the user's own SQL (LookupInput interpolates it), so a query that merely mentions the
		// token must not be routed here. Upstream's AssertFailedException::what() starts with it.
		if (err_text.rfind("AssertFailedException", 0) == 0) {
			throw duckdb::InternalException(err_text);
		}
		// pgr_alloc and to_pg_msg throw exactly "Out of memory!", which the driver turns into err
		// text; a std::bad_alloc raised anywhere inside pgRouting is instead caught by the
		// driver's catch(std::exception&) and lands here as what(), i.e. "std::bad_alloc".
		if (err_text == "Out of memory!" || err_text.find("bad_alloc") != std::string::npos) {
			throw duckdb::OutOfMemoryException(err_text);
		}
		throw duckdb::InvalidInputException(hint.empty() ? err_text : err_text + "\nHINT: " + hint);
	}

	// The logging macros expand to unqualified DuckDB names, and this is not namespace duckdb.
	using duckdb::DefaultLogType;
	using duckdb::Logger;
	using duckdb::LogLevel;

	// The two-argument macro form is the one that passes the message through unchanged: a third
	// argument would be read as a format parameter for the second.
	const auto notice_text = notice.str();
	if (!notice_text.empty()) {
		DUCKDB_LOG_INFO(context, notice_text);
	}
	const auto log_text = log.str();
	if (!log_text.empty()) {
		DUCKDB_LOG_DEBUG(context, log_text);
	}
	return result;
}

} // namespace duckdb_routing
