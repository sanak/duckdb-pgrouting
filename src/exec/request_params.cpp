// SPDX-License-Identifier: GPL-2.0-or-later

#include "pgrouting/request_params.hpp"

#include <variant>

#include "duckdb/common/exception.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"

namespace duckdb {

namespace {

using duckdb_pgrouting::DriverKind;
using duckdb_pgrouting::DriverRequest;

// A pointer to one DriverRequest field, of any type a field has.
using FieldPointer = std::variant<std::string DriverRequest::*, bool DriverRequest::*, int32_t DriverRequest::*,
                                  int64_t DriverRequest::*, double DriverRequest::*, char DriverRequest::*,
                                  DriverKind DriverRequest::*>;

struct RequestParameter {
	const char *name;
	FieldPointer field;
};

// The id lists (starts, ends, roots, via, methods, forbidden) are not here: they travel in the
// input row (input_slots.cpp).
const RequestParameter REQUEST_PARAMETERS[] = {
    {"edges_sql", &DriverRequest::edges_sql},
    {"points_sql", &DriverRequest::points_sql},
    {"combinations_sql", &DriverRequest::combinations_sql},
    {"restrictions_sql", &DriverRequest::restrictions_sql},
    {"matrix_sql", &DriverRequest::matrix_sql},
    {"coordinates_sql", &DriverRequest::coordinates_sql},
    {"directed", &DriverRequest::directed},
    {"only_cost", &DriverRequest::only_cost},
    {"normal", &DriverRequest::normal},
    {"n_goals", &DriverRequest::n_goals},
    {"global", &DriverRequest::global},
    {"driving_side", &DriverRequest::driving_side},
    {"details", &DriverRequest::details},
    {"which", &DriverRequest::which},
    {"heuristic", &DriverRequest::heuristic},
    {"factor", &DriverRequest::factor},
    {"epsilon", &DriverRequest::epsilon},
    {"distance", &DriverRequest::distance},
    {"equicost", &DriverRequest::equicost},
    {"max_depth", &DriverRequest::max_depth},
    {"mst_suffix", &DriverRequest::mst_suffix},
    {"k", &DriverRequest::k},
    {"heap_paths", &DriverRequest::heap_paths},
    {"stop_on_first", &DriverRequest::stop_on_first},
    {"strict", &DriverRequest::strict},
    {"u_turn_on_edge", &DriverRequest::u_turn_on_edge},
    {"start_id", &DriverRequest::start_id},
    {"end_id", &DriverRequest::end_id},
    {"root", &DriverRequest::root},
    {"algorithm", &DriverRequest::algorithm},
    {"cycles", &DriverRequest::cycles},
    {"driver", &DriverRequest::driver},
};

Value ToValue(const std::string &field) {
	return Value(field);
}
Value ToValue(bool field) {
	return Value::BOOLEAN(field);
}
Value ToValue(int32_t field) {
	return Value::INTEGER(field);
}
Value ToValue(int64_t field) {
	return Value::BIGINT(field);
}
Value ToValue(double field) {
	return Value::DOUBLE(field);
}
// Upstream's CHAR(1); DuckDB has no CHAR, so it travels as a one-character VARCHAR.
Value ToValue(char field) {
	return Value(string(1, field));
}
Value ToValue(DriverKind field) {
	return Value(string(duckdb_pgrouting::InfoOf(field).name));
}

void FromValue(const Value &value, std::string &field) {
	field = value.GetValue<string>();
}
void FromValue(const Value &value, bool &field) {
	field = value.GetValue<bool>();
}
void FromValue(const Value &value, int32_t &field) {
	field = value.GetValue<int32_t>();
}
void FromValue(const Value &value, int64_t &field) {
	field = value.GetValue<int64_t>();
}
void FromValue(const Value &value, double &field) {
	field = value.GetValue<double>();
}
// Only the first character reaches the driver, as upstream's process layer passes driving_side[0].
void FromValue(const Value &value, char &field) {
	const auto text = value.GetValue<string>();
	field = text.empty() ? ' ' : text[0];
}
void FromValue(const Value &value, DriverKind &field) {
	const auto name = value.GetValue<string>();
	const auto *info = duckdb_pgrouting::FindDriver(name);
	if (!info) {
		throw InvalidInputException("_pgr_exec: unknown driver '%s'", name);
	}
	field = info->kind;
}

// The type of the constant a default request's field becomes.
LogicalType TypeOf(const FieldPointer &field) {
	const DriverRequest defaults;
	return std::visit([&](auto member) { return ToValue(defaults.*member).type(); }, field);
}

} // namespace

void RegisterRequestParameters(TableFunction &exec) {
	for (const auto &parameter : REQUEST_PARAMETERS) {
		exec.named_parameters[parameter.name] = TypeOf(parameter.field);
	}
}

void ReadRequestParameters(const named_parameter_map_t &named, DriverRequest &request) {
	for (const auto &parameter : REQUEST_PARAMETERS) {
		auto it = named.find(parameter.name);
		if (it == named.end() || it->second.IsNull()) {
			continue;
		}
		SetRequestParameter(request, parameter.name, it->second);
	}
}

vector<unique_ptr<ParsedExpression>> RequestArguments(const DriverRequest &request) {
	vector<unique_ptr<ParsedExpression>> args;
	for (const auto &parameter : REQUEST_PARAMETERS) {
		auto value = std::visit([&](auto member) { return ToValue(request.*member); }, parameter.field);
		auto expr = make_uniq<ConstantExpression>(std::move(value));
		expr->SetAlias(string(parameter.name));
		args.push_back(std::move(expr));
	}
	return args;
}

bool SetRequestParameter(DriverRequest &request, const string &name, const Value &value) {
	for (const auto &parameter : REQUEST_PARAMETERS) {
		if (name == parameter.name) {
			std::visit([&](auto member) { FromValue(value, request.*member); }, parameter.field);
			return true;
		}
	}
	return false;
}

bool IsRequestParameter(const string &name) {
	for (const auto &parameter : REQUEST_PARAMETERS) {
		if (name == parameter.name) {
			return true;
		}
	}
	return false;
}

} // namespace duckdb
