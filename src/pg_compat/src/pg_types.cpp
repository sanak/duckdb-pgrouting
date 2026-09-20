// SPDX-License-Identifier: GPL-2.0-or-later

// The PostgreSQL-side definitions that upstream objects reference at link time: the allocator
// family used by pgr_alloc, the message duplicator, the ereport shim, and the ArrayType owner
// that the exec layer drives through driver_input.hpp.

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

#include "routing/pg_types.hpp"
#include "routing/driver_input.hpp"
#include "cpp_common/alloc.hpp"

namespace {
thread_local std::string ereport_message;

std::string FormatMessage(const char *fmt, va_list args) {
	char buffer[1024];
	std::vsnprintf(buffer, sizeof(buffer), fmt, args);
	return buffer;
}
} // namespace

extern "C" {

void *SPI_palloc(size_t size) {
	return std::malloc(size);
}

void *SPI_repalloc(void *pointer, size_t size) {
	return std::realloc(pointer, size);
}

void SPI_pfree(void *pointer) {
	std::free(pointer);
}

int errmsg(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	ereport_message = FormatMessage(fmt, args);
	va_end(args);
	return 0;
}

int errhint(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	ereport_message += "\nHINT: " + FormatMessage(fmt, args);
	va_end(args);
	return 0;
}

void pgr_compat_ereport(int) {
	std::string message;
	message.swap(ereport_message);
	throw message;
}

} // extern "C"

namespace pgrouting {

char *to_pg_msg(const std::string &msg) {
	if (msg.empty()) {
		return nullptr;
	}
	char *duplicate = static_cast<char *>(std::malloc(msg.size() + 1));
	if (!duplicate) {
		throw std::string("Out of memory!");
	}
	std::memcpy(duplicate, msg.c_str(), msg.size() + 1);
	return duplicate;
}

char *to_pg_msg(const std::ostringstream &msg) {
	return to_pg_msg(msg.str());
}

} // namespace pgrouting

namespace duckdb_routing {

ScopedIntArray::ScopedIntArray(const std::vector<int64_t> &values) : ptr(new ArrayType {values}) {
}

ScopedIntArray::ScopedIntArray() : ptr(nullptr) {
}

ScopedIntArray::~ScopedIntArray() {
	delete ptr;
}

} // namespace duckdb_routing
