// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Shadows third_party/pgrouting/include/cpp_common/interruption.hpp, whose body is PostgreSQL's
// CHECK_FOR_INTERRUPTS from miscadmin.h.

#include "pgrouting/input_access.hpp"

#ifdef _MSC_VER
#define __PGR_PRETTY_FUNCTION__ __FUNCSIG__
#else
#define __PGR_PRETTY_FUNCTION__ __PRETTY_FUNCTION__
#endif

#define CHECK_FOR_INTERRUPTS() ::duckdb_pgrouting::CheckForInterrupts()
