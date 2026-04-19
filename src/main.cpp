#include "ast.hpp"
#include "ast_lower.hpp"
#include "diagnostic.hpp"
#include "semantic_scope.hpp"
#include "semantic_type.hpp"
#include "semantic_usage.hpp"
#include "symbol_table.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "source.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;

void printUsage(const char* executableName) {
    std::cerr << "Usage: " << executableName << " [-q|--quiet] <source-file>\n";
    std::cerr << "  Verbose traces (lexer, parser, semantic) are on by default; `-q` disables them.\n";
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
            std::cout << "[driver] Lex failed with " << diagnostics.errorCount() << " error(s), "
                      << diagnostics.warningCount() << " warning(s), " << diagnostics.hintCount()
                      << " hint(s); parser step is skipped.\n";
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
            std::cout << "[driver] Parse failed: " << diagnostics.errorCount()
                      << " error(s). CST was not printed.\n";
        }
        return kExitFailure;
    }

    std::vector<std::unique_ptr<pavic::AstProgram>> astPrograms;
    astPrograms.reserve(programs.size());
    for (std::size_t i = 0; i < programs.size(); ++i) {
        if (programs[i]) {
            astPrograms.push_back(pavic::lowerCstToAst(sourceMap, tokens, *programs[i], !quiet));
        } else {
            astPrograms.push_back(nullptr);
        }
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
    }

    const std::size_t errorsBeforeSemantic = diagnostics.errorCount();
    std::vector<std::vector<pavic::SymbolRecord>> symbolTables;
    symbolTables.reserve(astPrograms.size());
    std::vector<bool> scopeOkPerProgram;
    scopeOkPerProgram.reserve(astPrograms.size());

    for (std::size_t i = 0; i < astPrograms.size(); ++i) {
        std::vector<pavic::SymbolRecord> table;
        const std::size_t before = diagnostics.errorCount();
        if (astPrograms[i]) {
            pavic::runScopeCheck(*astPrograms[i], sourceMap, tokens, diagnostics, !quiet, table);
        }
        symbolTables.push_back(std::move(table));
        scopeOkPerProgram.push_back(astPrograms[i] && diagnostics.errorCount() == before);
    }

    const std::size_t errorsAfterScope = diagnostics.errorCount();

    std::vector<bool> typeOkPerProgram;
    typeOkPerProgram.reserve(astPrograms.size());
    for (std::size_t i = 0; i < astPrograms.size(); ++i) {
        bool typeOk = false;
        if (astPrograms[i] && scopeOkPerProgram[i]) {
            const std::size_t beforeType = diagnostics.errorCount();
            pavic::runTypeCheck(*astPrograms[i], sourceMap, tokens, diagnostics, !quiet);
            typeOk = diagnostics.errorCount() == beforeType;
        }
        typeOkPerProgram.push_back(typeOk);
    }

    for (std::size_t i = 0; i < astPrograms.size(); ++i) {
        if (astPrograms[i] && typeOkPerProgram[i]) {
            pavic::runUsageAndInitHints(*astPrograms[i], sourceMap, tokens, diagnostics, !quiet);
        }
    }

    for (const auto& diagnostic : diagnostics.all()) {
        std::cerr << pavic::formatDiagnostic(sourcePath, diagnostic) << "\n";
    }

    if (diagnostics.errorCount() > errorsBeforeSemantic) {
        if (!quiet) {
            const std::size_t scopeErrs = errorsAfterScope - errorsBeforeSemantic;
            const std::size_t typeErrs = diagnostics.errorCount() - errorsAfterScope;
            std::cout << "[driver] Semantic check failed: " << (diagnostics.errorCount() - errorsBeforeSemantic)
                      << " error(s) (scope: " << scopeErrs << ", type: " << typeErrs
                      << "). Abstract syntax tree and symbol table were not printed; code generation is skipped.\n";
        }
        return kExitFailure;
    }

    if (!quiet) {
        std::cout << "========== Abstract Syntax Tree ==========\n";
        for (std::size_t i = 0; i < astPrograms.size(); ++i) {
            if (i > 0) {
                std::cout << "\n--- program " << (i + 1) << " (AST) ---\n";
            }
            if (astPrograms[i]) {
                astPrograms[i]->print(std::cout, 0);
            }
        }
        std::cout << "========== end AST ==========\n";

        std::cout << "========== Symbol table (scope + type) ==========\n";
        for (std::size_t i = 0; i < symbolTables.size(); ++i) {
            if (i > 0) {
                std::cout << "\n--- program " << (i + 1) << " ---\n";
            }
            pavic::printSymbolTable(std::cout, symbolTables[i]);
        }
        std::cout << "========== end symbol table ==========\n";

        std::cout << "[driver] Code generation: not implemented (semantic checks passed; nothing emitted).\n";

        std::cout << "[driver] Parse, scope, type, and usage checks succeeded (" << programs.size()
                  << " program(s)); warnings: "
                  << diagnostics.warningCount() << ", hints: " << diagnostics.hintCount()
                  << ". (Only errors block AST, symbol table, and codegen; warnings and hints are informational.)\n";
    }

    return kExitSuccess;
}
