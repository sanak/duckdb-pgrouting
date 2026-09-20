// SPDX-License-Identifier: GPL-2.0-or-later
// Stub for PostgreSQL's postgres.h. It declares only the names that the reused pgRouting code
// actually refers to; everything else is left undefined on purpose, so a new upstream dependency
// on PostgreSQL fails at compile time instead of misbehaving at run time.
#ifndef ROUTING_PG_COMPAT_POSTGRES_H
#define ROUTING_PG_COMPAT_POSTGRES_H

typedef struct HeapTupleData *HeapTuple;
typedef struct TupleDescData *TupleDesc;
typedef struct ArrayType ArrayType;

// ereport(ERROR, (errmsg(...), errhint(...))) is reached only from pgdata_fetchers.cpp
// (fetch_vehicle). errmsg/errhint format and collect the text; pgr_compat_ereport throws it as
// std::string, which is how pgRouting reports its own errors.
#define ERROR 21

#ifdef __cplusplus
// Own extern "C" guard: this header is reached both inside and outside upstream's extern "C"
// blocks, and the declarations must have the same linkage either way.
extern "C" {
#endif
int errmsg(const char *fmt, ...);
int errhint(const char *fmt, ...);
void pgr_compat_ereport(int elevel);
#ifdef __cplusplus
}
#endif

#define ereport(elevel, ...) (__VA_ARGS__, pgr_compat_ereport(elevel))

#endif /* ROUTING_PG_COMPAT_POSTGRES_H */
