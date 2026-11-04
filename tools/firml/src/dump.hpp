#pragma once

#include "ast.hpp"

#include <iosfwd>

namespace Firmir {
namespace Fml {

/// Deterministic S-expression dump of a parsed file. Used for snapshot tests
/// and for eyeballing the AST; the same input always produces byte-identical
/// output. Example:
///   (file main.fml
///     (include <firmir/items/Rectangle.hpp> 1:1)
///     (object Rectangle 3:1
///       (property width 4:5 (number 640))))
void dumpAst(const File& file, std::ostream& out);

} // namespace Fml
} // namespace Firmir
