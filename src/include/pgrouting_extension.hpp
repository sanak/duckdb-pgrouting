// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "duckdb.hpp"

namespace duckdb {

class PgroutingExtension : public Extension {
public:
	void Load(ExtensionLoader &loader) override;
	std::string Name() override;
	std::string Version() const override;
};

} // namespace duckdb
