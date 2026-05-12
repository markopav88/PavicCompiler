#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pavic {

class AstProgram;
class DiagnosticBag;
class SourceMap;
struct Token;

enum class BackendTarget {
    Target6502,
    TargetLlvmIr,
    TargetJavaSource,
    TargetTypeScriptSource,
};

bool parseBackendTarget(const std::string& value, BackendTarget& outTarget);
const char* backendTargetName(BackendTarget target);

bool emitLlvmIrModule(
    const std::vector<AstProgram*>& programs,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    DiagnosticBag& diagnostics,
    std::string& outText
);

bool emitJavaSourceModule(
    const std::vector<AstProgram*>& programs,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    DiagnosticBag& diagnostics,
    std::string& outText
);

bool emitTypeScriptModule(
    const std::vector<AstProgram*>& programs,
    const SourceMap& map,
    const std::vector<Token>& tokens,
    DiagnosticBag& diagnostics,
    std::string& outText
);

} // namespace pavic

