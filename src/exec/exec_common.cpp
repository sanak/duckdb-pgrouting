// SPDX-License-Identifier: GPL-2.0-or-later

#include "pgrouting/exec_common.hpp"

#include <cstdlib>
#include <sstream>

#include "duckdb/common/exception.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/logging/logger.hpp"

#include "c_types/path_rt.h"
#include "drivers/shortestPath_driver.hpp"
#include "pgrouting/driver_input.hpp"
#include "pgrouting/family_drivers.hpp"
#include "pgrouting/input_registry.hpp"

namespace duckdb_pgrouting {

DriverResult::~DriverResult() {
	Release();
}

DriverResult::DriverResult(DriverResult &&other) noexcept
    : shape(other.shape), rows(other.rows), count(other.count), is_matrix(other.is_matrix) {
	other.rows = nullptr;
	other.count = 0;
}

DriverResult &DriverResult::operator=(DriverResult &&other) noexcept {
	if (this != &other) {
		Release();
		shape = other.shape;
		rows = other.rows;
		count = other.count;
		is_matrix = other.is_matrix;
		other.rows = nullptr;
		other.count = 0;
	}
	return *this;
}

// A shape whose rows own arrays of their own frees those here, before the rows themselves, so the
// error path that drops partial results releases them too.
void DriverResult::Release() {
	switch (shape) {
	case ResultShape::PATH:
	case ResultShape::PAIRS:
		break; // flat rows
	}
	std::free(rows);
	rows = nullptr;
	count = 0;
}

DriverResult RunDriver(duckdb::ClientContext &context, InputRegistry &registry, const DriverRequest &request) {
	DriverResult result;
	result.shape = InfoOf(request.driver).shape;
	std::string log_text;
	std::string notice_text;
	std::string err_text;

	ScopedIntArray starts(request.starts);
	ScopedIntArray ends(request.ends);
	DriverArrays arrays;
	arrays.starts = request.has_starts ? starts.get() : nullptr;
	arrays.ends = request.has_ends ? ends.get() : nullptr;

	{
		ScopedRoutingContext scope(context, registry);
		if (request.driver == DriverKind::SHORTEST_PATH) {
			std::ostringstream log;
			std::ostringstream notice;
			std::ostringstream err;
			Path_rt *rows = nullptr;
			do_shortestPath(request.edges_sql, request.points_sql, request.combinations_sql, arrays.starts,
			                arrays.ends, request.directed, request.only_cost, request.normal, request.n_goals,
			                request.global, request.driving_side, request.details, request.which, result.is_matrix,
			                rows, result.count, log, notice, err);
			result.rows = rows;
			log_text = log.str();
			notice_text = notice.str();
			err_text = err.str();
		} else {
			// The families pgRouting has not moved onto do_shortestPath (family_drivers.cpp).
			// RunFamilyDriver itself checks the driver's rows against InfoOf(request.driver).shape
			// and reports a mismatch through out.err, handled by the err_text branch below; the
			// ownership of out.rows passes to `result` here either way, so an error still frees the
			// malloc'd rows (via ~DriverResult, on unwind) instead of leaking them.
			auto out = RunFamilyDriver(request, arrays);
			result.rows = out.rows;
			result.count = out.count;
			log_text = std::move(out.log);
			notice_text = std::move(out.notice);
			err_text = std::move(out.err);
		}

		// The drivers catch every exception, so an interrupt only shows up as a flag.
		if (WasInterrupted()) {
			ClearInterrupted();
			throw duckdb::InterruptException();
		}
	}

	if (!err_text.empty()) {
		// Upstream's process layer drops partial results when err is set; so do we.
		result = DriverResult();
		const auto &hint = log_text;
		// A prefix match, not a substring one: this is the only arm that raises
		// InternalException, which invalidates the whole database instance, and err text can carry
		// the user's own SQL (LookupInput interpolates it), so a query that merely mentions the
		// token must not be routed here. Upstream's AssertFailedException::what() starts with it.
		if (err_text.rfind("AssertFailedException", 0) == 0) {
			throw duckdb::InternalException(err_text);
		}
		// Exact comparisons, for the same reason the arm above is a prefix match: err text can
		// carry the user's own SQL, and neither producer here needs anything looser. pgr_alloc and
		// to_pg_msg throw exactly "Out of memory!", which the driver copies into err verbatim. A
		// std::bad_alloc raised anywhere inside pgRouting is instead caught by the driver's
		// catch (std::exception &), whose whole body is `err << except.what();` -- nothing before
		// it, nothing after it, into the stream this function just created -- so err is exactly
		// what(). The per-family drivers copy what() into their err message the same way. That
		// text is spelled by the standard library rather than by pgRouting; it is
		// "std::bad_alloc" on the libc++ toolchain this was verified against. A standard library
		// that spells it differently falls through to the InvalidInputException below, which is
		// where every unrecognised err text already goes and is what this arm used to do.
		if (err_text == "Out of memory!" || err_text == "std::bad_alloc") {
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
	if (!notice_text.empty()) {
		DUCKDB_LOG_INFO(context, notice_text);
	}
	if (!log_text.empty()) {
		DUCKDB_LOG_DEBUG(context, log_text);
	}
	return result;
}

} // namespace duckdb_pgrouting
