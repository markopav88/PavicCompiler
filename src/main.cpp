#include "diagnostic.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "source.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;

void printUsage(const char* executableName) {
    std::cerr << "Usage: " << executableName << " [-q|--quiet] <source-file>\n";
    std::cerr << "  (verbose lexer trace is the default; use -q to disable)\n";
}

bool readFileToString(const std::string& filePath, std::string& contents) {
    std::ifstream input(filePath, std::ios::binary);
    if (!input) {
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    contents = buffer.str();
    return true;
}

bool parseArguments(int argc, char** argv, bool& quiet, std::string& sourcePath) {
    quiet = false;

    if (argc == 2) {
        sourcePath = argv[1];
        return true;
    }

    if (argc == 3) {
        const std::string flag(argv[1]);
        if (flag == "-q" || flag == "--quiet") {
            quiet = true;
            sourcePath = argv[2];
            return true;
        }
    }

    return false;
}

} // namespace

int main(int argc, char** argv) {
    bool quiet = false;
    std::string sourcePath;

    if (!parseArguments(argc, argv, quiet, sourcePath)) {
        printUsage(argv[0]);
        return kExitFailure;
    }

    pavic::DiagnosticBag diagnostics;
    std::string sourceText;

    if (!readFileToString(sourcePath, sourceText)) {
        diagnostics.addError(
            "unable to open input file",
            {1, 1},
            "Check the file path and permissions, then try again."
        );
    }

    if (diagnostics.hasErrors()) {
        for (const auto& diagnostic : diagnostics.all()) {
            std::cerr << pavic::formatDiagnostic(sourcePath, diagnostic) << "\n";
        }
        return kExitFailure;
    }

    const pavic::SourceMap sourceMap(sourceText);
    std::vector<pavic::Token> tokens;
    pavic::Lexer lexer(sourceMap, diagnostics, !quiet);
    lexer.lexAll(tokens);

    for (const auto& diagnostic : diagnostics.all()) {
        std::cerr << pavic::formatDiagnostic(sourcePath, diagnostic) << "\n";
    }

    if (diagnostics.errorCount() > 0) {
        if (!quiet) {
            std::cout << "[driver] Lex failed with " << diagnostics.errorCount() << " error(s) and "
                      << diagnostics.warningCount() << " warning(s); parser step is skipped.\n";
        }
        return kExitFailure;
    }

    pavic::Parser parser(sourceMap, diagnostics, tokens, !quiet);
    parser.trace("parser driver initialized (token cursor at start of stream)");

    std::vector<std::unique_ptr<pavic::CstProgram>> programs = parser.parseTranslationUnit();

    for (const auto& diagnostic : diagnostics.all()) {
        std::cerr << pavic::formatDiagnostic(sourcePath, diagnostic) << "\n";
    }

    if (diagnostics.errorCount() > 0) {
        if (!quiet) {
            std::cout << "[driver] Parse failed with " << diagnostics.errorCount() << " error(s); CST not printed.\n";
        }
        return kExitFailure;
    }

    if (!quiet) {
        std::cout << "========== Concrete Syntax Tree ==========\n";
        for (std::size_t i = 0; i < programs.size(); ++i) {
            if (i > 0) {
                std::cout << "\n--- program " << (i + 1) << " ---\n";
            }
            if (programs[i]) {
                programs[i]->print(std::cout, 0);
            }
        }
        std::cout << "========== end CST ==========\n";
        std::cout << "[driver] Parse succeeded (" << programs.size()
                  << " program(s)); lex warnings: " << diagnostics.warningCount() << ".\n";
    }

    return kExitSuccess;
}
