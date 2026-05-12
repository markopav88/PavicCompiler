#include "ast.hpp"
#include "ast_lower.hpp"
#include "diagnostic.hpp"
#include "semantic_scope.hpp"
#include "semantic_type.hpp"
#include "semantic_usage.hpp"
#include "symbol_table.hpp"
#include "codegen/codegen.hpp"
#include "optimizer.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "source.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
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
    std::cerr << "Usage: " << executableName
              << " [-q|--quiet] [--fix] [--emulator] [--intel-hex] [--pad=N] [-o|--output <file>] <source-file>\n";
    std::cerr << "  Verbose traces (lexer, parser, semantic, codegen) are on by default; `-q` disables them.\n";
    std::cerr << "  `--fix` applies conservative `SourceRewrite` suggestions from lexer/parser diagnostics and re-runs lex/parse (bounded).\n";
    std::cerr << "  `-o` writes linked object bytes after successful codegen (all type-checked programs concatenated).\n";
    std::cerr << "  `--intel-hex` writes Intel HEX instead of raw binary (for loaders / web emulators).\n";
    std::cerr << "  `--emulator` emits EA at syscall sites (e-tradition NOP convention) instead of FF.\n";
    std::cerr << "  `--pad=N` pads the written object with $00 to the next multiple of N (N >= 1).\n";
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

bool parsePadMultiple(const char* s, std::uint32_t& outMultiple) {
    if (s == nullptr || s[0] == '\0') {
        return false;
    }
    std::uint64_t v = 0;
    for (const char* p = s; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        v = v * 10ULL + static_cast<std::uint64_t>(*p - '0');
        if (v > 0xFFFFFFFFULL) {
            return false;
        }
    }
    if (v == 0) {
        return false;
    }
    outMultiple = static_cast<std::uint32_t>(v);
    return true;
}

bool parseArguments(
    int argc,
    char** argv,
    bool& quiet,
    bool& applySuggestedFixes,
    std::string& sourcePath,
    std::string& outputPath,
    bool& intelHex,
    bool& emulator,
    std::uint32_t& padMultiple
) {
    quiet = false;
    applySuggestedFixes = false;
    outputPath.clear();
    intelHex = false;
    emulator = false;
    padMultiple = 0;
    sourcePath.clear();

    for (int i = 1; i < argc; ++i) {
        const std::string a(argv[i]);
        if (a == "-q" || a == "--quiet") {
            quiet = true;
        } else if (a == "--fix") {
            applySuggestedFixes = true;
        } else if (a == "-o" || a == "--output") {
            if (i + 1 >= argc) {
                return false;
            }
            outputPath = argv[++i];
        } else if (a == "--intel-hex" || a == "--ihex") {
            intelHex = true;
        } else if (a == "--emulator" || a == "--ea-sys") {
            emulator = true;
        } else if (a.rfind("--pad=", 0) == 0) {
            if (!parsePadMultiple(a.c_str() + 6, padMultiple)) {
                return false;
            }
        } else if (a[0] == '-') {
            return false;
        } else if (!sourcePath.empty()) {
            return false;
        } else {
            sourcePath = a;
        }
    }

    return !sourcePath.empty();
}

/// Sum of all decoded bytes in one Intel HEX record (length, addr hi/lo, type, data) must be 0 mod 256.
std::uint8_t intelHexRecordChecksum(std::uint8_t byteCount, std::uint16_t addr, std::uint8_t recordType, const std::uint8_t* data) {
    unsigned sum = byteCount;
    sum += (addr >> 8) & 0xFF;
    sum += addr & 0xFF;
    sum += recordType;
    for (std::uint8_t i = 0; i < byteCount; ++i) {
        sum += data[i];
    }
    return static_cast<std::uint8_t>((256U - (sum & 0xFFU)) & 0xFFU);
}

bool writeIntelHexFile(
    const std::string& path,
    std::uint16_t loadAddress,
    const std::vector<std::uint8_t>& bytes,
    pavic::DiagnosticBag& diagnostics
) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        diagnostics.addError(
            "unable to open output file for Intel HEX",
            {1, 1},
            "Check the `-o` path and permissions."
        );
        return false;
    }

    constexpr std::size_t kChunk = 16;
    std::uint32_t addr = loadAddress;
    for (std::size_t offset = 0; offset < bytes.size(); offset += kChunk) {
        const std::uint8_t n = static_cast<std::uint8_t>(std::min(kChunk, bytes.size() - offset));
        const std::uint16_t addr16 = static_cast<std::uint16_t>(addr & 0xFFFFU);
        const std::uint8_t* chunk = bytes.data() + offset;
        const std::uint8_t cs = intelHexRecordChecksum(n, addr16, 0x00, chunk);

        char lineStart[16];
        std::snprintf(lineStart, sizeof(lineStart), ":%02X%04X00", static_cast<unsigned>(n), static_cast<unsigned>(addr16));
        out << lineStart;
        for (std::uint8_t j = 0; j < n; ++j) {
            char b[4];
            std::snprintf(b, sizeof(b), "%02X", static_cast<unsigned>(chunk[j]));
            out << b;
        }
        char tail[4];
        std::snprintf(tail, sizeof(tail), "%02X\n", static_cast<unsigned>(cs));
        out << tail;
        addr += n;
    }

    // EOF record
    out << ":00000001FF\n";
    if (!out) {
        diagnostics.addError(
            "unable to write complete Intel HEX file",
            {1, 1},
            "The output path may be on a full disk or a device error may have occurred."
        );
        return false;
    }
    return true;
}

bool writeBinaryObjectFile(
    const std::string& path,
    const std::vector<std::uint8_t>& bytes,
    pavic::DiagnosticBag& diagnostics
) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        diagnostics.addError(
            "unable to open output file for object bytes",
            {1, 1},
            "Check the `-o` path and permissions."
        );
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        diagnostics.addError(
            "unable to write complete object file",
            {1, 1},
            "The output path may be on a full disk or a device error may have occurred."
        );
        return false;
    }
    return true;
}

void applyPadding(std::vector<std::uint8_t>& bytes, std::uint32_t padMultiple) {
    if (padMultiple == 0 || bytes.empty()) {
        return;
    }
    const std::size_t rem = bytes.size() % static_cast<std::size_t>(padMultiple);
    if (rem == 0) {
        return;
    }
    const std::size_t add = static_cast<std::size_t>(padMultiple) - rem;
    bytes.insert(bytes.end(), add, 0x00);
}

} // namespace

int main(int argc, char** argv) {
    constexpr int kMaxFixPasses = 32;
    bool quiet = false;
    bool applySuggestedFixes = false;
    std::string sourcePath;
    std::string outputPath;
    bool intelHex = false;
    bool emulator = false;
    std::uint32_t padMultiple = 0;

    if (!parseArguments(argc, argv, quiet, applySuggestedFixes, sourcePath, outputPath, intelHex, emulator, padMultiple)) {
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

    std::vector<pavic::Token> tokens;
    std::vector<std::unique_ptr<pavic::CstProgram>> programs;
    bool parseSucceeded = false;

    for (int fixPass = 0; fixPass < kMaxFixPasses; ++fixPass) {
        diagnostics.clear();
        const pavic::SourceMap sourceMap(sourceText);
        tokens.clear();
        pavic::Lexer lexer(sourceMap, diagnostics, !quiet);
        lexer.lexAll(tokens);

        for (const auto& diagnostic : diagnostics.all()) {
            std::cerr << pavic::formatDiagnostic(sourcePath, diagnostic, &sourceMap) << "\n";
        }

        if (diagnostics.errorCount() > 0) {
            if (applySuggestedFixes) {
                std::vector<pavic::SourceRewrite> fixes = pavic::collectSuggestedFixes(diagnostics);
                if (!fixes.empty()) {
                    std::string fixErr;
                    if (pavic::applySourceRewritesToString(sourceText, std::move(fixes), &fixErr)) {
                        if (!quiet) {
                            std::cout << "[driver] --fix: applied rewrite(s) after lex errors; restarting (attempt "
                                      << (fixPass + 1) << ").\n";
                        }
                        continue;
                    }
                    if (!quiet) {
                        std::cout << "[driver] --fix: could not apply rewrite(s): " << fixErr << "\n";
                    }
                }
            }
            if (!quiet) {
                std::cout << "[driver] Lex failed with " << diagnostics.errorCount() << " error(s), "
                          << diagnostics.warningCount() << " warning(s), " << diagnostics.hintCount()
                          << " hint(s); parser step is skipped.\n";
            }
            return kExitFailure;
        }

        if (applySuggestedFixes) {
            std::vector<pavic::SourceRewrite> fixes = pavic::collectSuggestedFixes(diagnostics);
            if (!fixes.empty()) {
                std::string fixErr;
                if (pavic::applySourceRewritesToString(sourceText, std::move(fixes), &fixErr)) {
                    if (!quiet) {
                        std::cout << "[driver] --fix: applied rewrite(s) after successful lex; restarting (attempt "
                                  << (fixPass + 1) << ").\n";
                    }
                    continue;
                }
                if (!quiet) {
                    std::cout << "[driver] --fix: could not apply rewrite(s): " << fixErr << "\n";
                }
            }
        }

        pavic::Parser parser(sourceMap, diagnostics, tokens, !quiet);
        parser.trace("parser driver initialized (token cursor at start of stream)");
        programs = parser.parseTranslationUnit();

        for (const auto& diagnostic : diagnostics.all()) {
            std::cerr << pavic::formatDiagnostic(sourcePath, diagnostic, &sourceMap) << "\n";
        }

        if (diagnostics.errorCount() > 0) {
            if (applySuggestedFixes) {
                std::vector<pavic::SourceRewrite> fixes = pavic::collectSuggestedFixes(diagnostics);
                if (!fixes.empty()) {
                    std::string fixErr;
                    if (pavic::applySourceRewritesToString(sourceText, std::move(fixes), &fixErr)) {
                        if (!quiet) {
                            std::cout << "[driver] --fix: applied rewrite(s) after parse errors; restarting (attempt "
                                      << (fixPass + 1) << ").\n";
                        }
                        continue;
                    }
                    if (!quiet) {
                        std::cout << "[driver] --fix: could not apply rewrite(s): " << fixErr << "\n";
                    }
                }
            }
            if (!quiet) {
                std::cout << "[driver] Parse failed: " << diagnostics.errorCount()
                          << " error(s). CST was not printed.\n";
            }
            return kExitFailure;
        }

        parseSucceeded = true;
        break;
    }

    if (!parseSucceeded) {
        std::cerr << "[driver] --fix: exceeded maximum rewrite attempts (" << kMaxFixPasses << ").\n";
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
    }

    const pavic::SourceMap sourceMap(sourceText);

    std::vector<std::unique_ptr<pavic::AstProgram>> astPrograms;
    astPrograms.reserve(programs.size());
    for (std::size_t i = 0; i < programs.size(); ++i) {
        if (programs[i]) {
            astPrograms.push_back(pavic::lowerCstToAst(sourceMap, tokens, *programs[i], !quiet));
        } else {
            astPrograms.push_back(nullptr);
        }
    }

    const std::size_t errorsBeforeSemantic = diagnostics.errorCount();
    std::vector<std::vector<pavic::SymbolRecord>> symbolTables;
    symbolTables.reserve(astPrograms.size());
    std::vector<std::vector<pavic::ScopeRecord>> scopeTrees;
    scopeTrees.reserve(astPrograms.size());
    std::vector<bool> scopeOkPerProgram;
    scopeOkPerProgram.reserve(astPrograms.size());

    for (std::size_t i = 0; i < astPrograms.size(); ++i) {
        std::vector<pavic::SymbolRecord> table;
        std::vector<pavic::ScopeRecord> scopes;
        const std::size_t before = diagnostics.errorCount();
        if (astPrograms[i]) {
            pavic::runScopeCheck(*astPrograms[i], sourceMap, tokens, diagnostics, !quiet, table, scopes);
        }
        symbolTables.push_back(std::move(table));
        scopeTrees.push_back(std::move(scopes));
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

    const std::size_t errorsAfterType = diagnostics.errorCount();

    for (std::size_t i = 0; i < astPrograms.size(); ++i) {
        if (astPrograms[i] && typeOkPerProgram[i]) {
            pavic::optimizeAstProgram(*astPrograms[i], !quiet);
        }
    }

    for (std::size_t i = 0; i < astPrograms.size(); ++i) {
        if (astPrograms[i] && typeOkPerProgram[i]) {
            pavic::runUsageAndInitHints(*astPrograms[i], sourceMap, tokens, diagnostics, !quiet);
        }
    }

    const std::size_t errorsAfterUsage = diagnostics.errorCount();

    pavic::codegen::CodeGenTarget codegenTarget{};
    if (emulator) {
        codegenTarget.sysEncoding = pavic::codegen::SysCallEncoding::EmulatorNopEA;
    }

    std::vector<std::vector<std::uint8_t>> objectCodePerProgram;
    objectCodePerProgram.resize(astPrograms.size());
    if (!quiet) {
        std::cout << "Begin CODE GENERATION\n\n";
    }
    for (std::size_t i = 0; i < astPrograms.size(); ++i) {
        if (astPrograms[i] && typeOkPerProgram[i]) {
            pavic::generate6502Program(
                *astPrograms[i],
                sourceMap,
                tokens,
                symbolTables[i],
                codegenTarget,
                diagnostics,
                !quiet,
                objectCodePerProgram[i]
            );
        }
    }
    if (!quiet) {
        const std::size_t codegenErrors = diagnostics.errorCount() - errorsAfterUsage;
        std::cout << "\nCODE GEN complete with " << codegenErrors << " errors\n\n";
    }

    std::vector<std::uint8_t> linkedObject;
    if (!outputPath.empty() && diagnostics.errorCount() == errorsBeforeSemantic) {
        for (std::size_t i = 0; i < objectCodePerProgram.size(); ++i) {
            const auto& chunk = objectCodePerProgram[i];
            if (!chunk.empty()) {
                linkedObject.insert(linkedObject.end(), chunk.begin(), chunk.end());
            }
        }
        applyPadding(linkedObject, padMultiple);
        const std::uint16_t loadBase = codegenTarget.ramBase;
        if (intelHex) {
            writeIntelHexFile(outputPath, loadBase, linkedObject, diagnostics);
        } else {
            writeBinaryObjectFile(outputPath, linkedObject, diagnostics);
        }
        if (!quiet && diagnostics.errorCount() == errorsBeforeSemantic) {
            std::cout << "[driver] Wrote object file `" << outputPath << "` (" << linkedObject.size() << " byte(s), load base $";
            char baseBuf[8];
            std::snprintf(baseBuf, sizeof(baseBuf), "%04X", static_cast<unsigned>(loadBase));
            std::cout << baseBuf;
            std::cout << (intelHex ? ", Intel HEX" : ", raw binary");
            if (padMultiple != 0) {
                std::cout << ", padded with $00 to a multiple of " << padMultiple;
            }
            std::cout << ").\n";
        }
    }

    for (const auto& diagnostic : diagnostics.all()) {
        std::cerr << pavic::formatDiagnostic(sourcePath, diagnostic, &sourceMap) << "\n";
    }

    if (diagnostics.errorCount() > errorsBeforeSemantic) {
        if (!quiet) {
            const std::size_t scopeErrs = errorsAfterScope - errorsBeforeSemantic;
            const std::size_t typeErrs = errorsAfterType - errorsAfterScope;
            const std::size_t codegenErrs = diagnostics.errorCount() - errorsAfterUsage;
            std::cout << "[driver] Semantic/codegen check failed: " << (diagnostics.errorCount() - errorsBeforeSemantic)
                      << " error(s) (scope: " << scopeErrs << ", type: " << typeErrs << ", codegen: " << codegenErrs
                      << "). Abstract syntax tree and symbol table were not printed; code generation is incomplete.\n";
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
            std::cout << "Scope tree:\n";
            pavic::printScopeTree(std::cout, scopeTrees[i]);
            std::cout << "Declarations:\n";
            pavic::printSymbolTable(std::cout, symbolTables[i]);
        }
        std::cout << "========== end symbol table ==========\n";

        std::cout << "6502 Code\n\n";
        for (std::size_t i = 0; i < objectCodePerProgram.size(); ++i) {
            if (i > 0) {
                std::cout << "\n--- program " << (i + 1) << " ---\n";
            }
            const auto& bytes = objectCodePerProgram[i];
            if (bytes.empty()) {
                std::cout << "(no object code; program skipped or not type-checked)\n";
                continue;
            }
            for (std::size_t b = 0; b < bytes.size(); ++b) {
                if (b != 0) {
                    std::cout << ' ';
                }
                char buf[4];
                std::snprintf(buf, sizeof(buf), "%02X", static_cast<unsigned>(bytes[b]));
                std::cout << buf;
            }
            std::cout << "\n";
        }
        std::cout << "\nEND\n";

        std::cout << "[driver] Parse, scope, type, usage, and codegen succeeded (" << programs.size()
                      << " program(s)); warnings: "
                      << diagnostics.warningCount() << ", hints: " << diagnostics.hintCount()
                      << ". (Only errors block AST, symbol table, and object listing; warnings and hints are informational.)\n";
    }

    return kExitSuccess;
}
