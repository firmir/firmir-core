#pragma once

#include "ast.hpp"
#include "sema.hpp"

#include <string>

namespace Firmir {
namespace Fml {

struct GeneratedFile {
    std::string headerName; // <stem>.hpp
    std::string headerText;
    std::string sourceName; // <stem>.cpp
    std::string sourceText;
};

/// Generates C++ (header + source) for an analyzed .fml file: static
/// properties (M3), property bindings (M4) and behavior animations (M5).
/// outputStem may contain '/' (e.g. "views/panel"); when empty, the input
/// basename and legacy .hpp extension are used.
GeneratedFile generateCode(const File& file, const SemaResult& sema,
                           const std::string& outputStem = {});

} // namespace Fml
} // namespace Firmir
