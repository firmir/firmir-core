#include "codegen.hpp"
#include "dump.hpp"
#include "error.hpp"
#include "parser.hpp"
#include "sema.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void printUsage(std::ostream& out) {
    out << "firmlc - Firml (.fml) to C++ transpiler (M3: C++ code generation)\n"
           "\n"
           "Usage: firmlc [--dump-ast | --dump-sema | --dump-gen] <input.fml>\n"
           "              [-I <path>]... [-o <dir>]\n"
           "\n"
           "Options:\n"
           "  --dump-ast   parse only; print the AST to stdout (no semantic checks)\n"
           "  --dump-sema  parse and analyze; print the semantic summary to stdout\n"
           "  --dump-gen   parse, analyze and print the generated C++ (hpp+cpp) to stdout\n"
           "  -I <path>    add an include search path for #include <...> resolution\n"
           "  -o <dir>     output directory; the generated <name>.hpp/.cpp are written\n"
           "               there (default mode; without -o the code goes to stdout)\n"
           "  -h, --help   show this help\n"
           "\n"
           "Exit codes: 0 = ok (warnings on stderr), 1 = Firml error (file:line:col on\n"
           "stderr), 2 = usage/IO error.\n";
}

} // namespace

int main(int argc, char** argv) {
    using namespace Firmir::Fml;

    std::string input;
    std::string outDir;
    std::string rootDir;
    std::vector<std::string> includePaths;
    bool dumpAstMode = false;
    bool dumpSemaMode = false;
    bool dumpGenMode = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(std::cout);
            return 0;
        }
        if (arg == "--dump-ast") {
            dumpAstMode = true;
            continue;
        }
        if (arg == "--dump-sema") {
            dumpSemaMode = true;
            continue;
        }
        if (arg == "--dump-gen") {
            dumpGenMode = true;
            continue;
        }
        if (arg == "-I") {
            if (i + 1 >= argc) {
                std::cerr << "firmlc: -I needs a path\n";
                return 2;
            }
            includePaths.emplace_back(argv[++i]);
            continue;
        }
        if (arg == "--root-dir") {
            if (i + 1 >= argc) {
                std::cerr << "firmlc: --root-dir needs a directory\n";
                return 2;
            }
            rootDir = argv[++i];
            continue;
        }
        if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "firmlc: -o needs a directory\n";
                return 2;
            }
            outDir = argv[++i];
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "firmlc: unknown option '" << arg << "'\n";
            return 2;
        }
        if (!input.empty()) {
            std::cerr << "firmlc: multiple input files; expected exactly one\n";
            return 2;
        }
        input = arg;
    }

    if (input.empty()) {
        printUsage(std::cerr);
        return 2;
    }

    std::ifstream in(input);
    if (!in) {
        std::cerr << "firmlc: cannot open '" << input << "'\n";
        return 2;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string source = buffer.str();

    if (static_cast<int>(dumpAstMode) + static_cast<int>(dumpSemaMode) +
            static_cast<int>(dumpGenMode) >
        1) {
        std::cerr << "firmlc: --dump-ast, --dump-sema and --dump-gen are mutually "
                     "exclusive\n";
        return 2;
    }

    File file;
    SemaResult sema;
    try {
        Parser parser(source, input);
        file = parser.parseFile();
        if (!dumpAstMode)
            analyzeFile(file, includePaths, sema);
    } catch (const Error& error) {
        std::cerr << formatError(error) << "\n";
        return 1;
    }

    for (const SemaWarning& warning : sema.warnings)
        std::cerr << formatWarning(warning.loc, warning.message) << "\n";

    if (dumpAstMode) {
        dumpAst(file, std::cout);
        return 0;
    }
    if (dumpSemaMode) {
        dumpSema(file, sema, std::cout);
        return 0;
    }

    // Default mode: C++ code generation (hpp + cpp).
    std::string outputStem;
    if (!rootDir.empty()) {
        std::error_code relativeError;
        outputStem = std::filesystem::relative(
                         std::filesystem::path(input), std::filesystem::path(rootDir),
                         relativeError)
                         .string();
        if (relativeError || outputStem.empty() || outputStem.compare(0, 2, "..") == 0) {
            std::cerr << "firmlc: input is outside --root-dir\n";
            return 2;
        }
        outputStem = std::filesystem::path(outputStem).replace_extension().string();
    }
    const GeneratedFile generated = generateCode(file, sema, outputStem);
    if (dumpGenMode || outDir.empty()) {
        std::cout << "=== " << generated.headerName << " ===\n"
                  << generated.headerText << "=== " << generated.sourceName
                  << " ===\n"
                  << generated.sourceText;
        return 0;
    }

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        std::cerr << "firmlc: cannot create output directory '" << outDir
                  << "': " << ec.message() << "\n";
        return 2;
    }
    const std::pair<const std::string*, const std::string*> outputs[] = {
        {&generated.headerName, &generated.headerText},
        {&generated.sourceName, &generated.sourceText},
    };
    for (const auto& entry : outputs) {
        const std::string outPath =
            (std::filesystem::path(outDir) / *entry.first).string();
        std::filesystem::create_directories(
            std::filesystem::path(outPath).parent_path(), ec);
        if (ec) {
            std::cerr << "firmlc: cannot create parent directory for '" << outPath
                      << "': " << ec.message() << "\n";
            return 2;
        }
        std::ofstream out(outPath);
        if (!out) {
            std::cerr << "firmlc: cannot write '" << outPath << "'\n";
            return 2;
        }
        out << *entry.second;
    }

    return 0;
}
