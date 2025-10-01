// #pragma once

// #include "duckdb.hpp"

// namespace duckdb {

// class MiniplotExtension : public Extension {
// public:
// 	void Load(ExtensionLoader &loader) override;
// 	std::string Name() override;
// 	std::string Version() const override;
// };

// } // namespace duckdb

#pragma once

#include "duckdb.hpp"

namespace duckdb {

class MiniplotExtension : public Extension {
public:
    void Load(ExtensionLoader &loader) override;  // これが正解
    std::string Name() override;
    std::string Version() const override;
};

} // namespace duckdb
