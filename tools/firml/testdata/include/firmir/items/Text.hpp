#pragma once

// Test stub for firmlc include resolution (M2). The real header lives in
// firmir-gui; firmlc only checks that the file exists and declares the type
// named after the file (convention: one public type per header).
namespace Firmir {
namespace Gui {

class Text {};

} // namespace Gui
} // namespace Firmir
