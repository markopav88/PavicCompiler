# PavicCompiler — build with GNU g++ and C++17
CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic
CPPFLAGS := -Isrc

SOURCES := \
	src/main.cpp \
	src/diagnostic.cpp \
	src/source.cpp \
	src/token.cpp \
	src/lexer.cpp \
	src/cst.cpp \
	src/parser.cpp \
	src/ast.cpp \
	src/ast_lower.cpp \
	src/optimizer.cpp \
	src/symbol_table.cpp \
	src/semantic_scope.cpp \
	src/semantic_type.cpp \
	src/semantic_usage.cpp \
	src/codegen/memory_layout.cpp \
	src/codegen/code_buffer.cpp \
	src/codegen/codegen.cpp \
	src/multi_backend.cpp

TARGET := pavicc

.PHONY: all clean test phase-tests

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(SOURCES)

# Codegen regression: `3+4` via `z = 3+4`, emulator syscall (EA), epilogue BRK — byte-for-byte vs committed .expected.bin
test: $(TARGET) tests/regression/add34.markos tests/regression/add34.expected.bin
	./$(TARGET) -q --emulator -o /tmp/pavic_regression_add34.bin tests/regression/add34.markos
	cmp /tmp/pavic_regression_add34.bin tests/regression/add34.expected.bin

phase-tests: $(TARGET) tests/run_phase_tests.sh
	./tests/run_phase_tests.sh

clean:
	rm -f $(TARGET)
