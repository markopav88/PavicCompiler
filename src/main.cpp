#include "diagnostic.hpp"
#include "source.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;

void printUsage(const char* executableName) {
    std::cerr << "Usage: " << executableName << " <source-file>\n";
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

} // namespace

int main(int argc, char** argv) {
    pavic::DiagnosticBag diagnostics;

    if (argc != 2) {
        printUsage(argv[0]);
        diagnostics.addError(
            "expected exactly one source file path argument",
            {1, 1},
            "Run the compiler as: pavicc <path-to-source-file>"
        );
    }

    std::string sourcePath = argc >= 2 ? argv[1] : "<missing>";
    std::string sourceText;

    if (argc == 2 && !readFileToString(sourcePath, sourceText)) {
        diagnostics.addError(
            "unable to open input file",
            {1, 1},
            "Check the file path and permissions, then try again."
        );
    }

    for (const auto& diagnostic : diagnostics.all()) {
        std::cerr << pavic::formatDiagnostic(sourcePath, diagnostic) << "\n";
    }

    if (diagnostics.hasErrors()) {
        return kExitFailure;
    }

    const pavic::SourceMap sourceMap(sourceText);
    const pavic::SourceLocation eofLocation = sourceMap.locationAt(sourceMap.byteLength());

    std::cout << "Loaded source file successfully (" << sourceMap.byteLength() << " bytes, "
              << sourceMap.lineCount() << " lines; EOF at " << eofLocation.line << ":"
              << eofLocation.column << ").\n";
    return kExitSuccess;
}
