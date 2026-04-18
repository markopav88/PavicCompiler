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
	src/ast_lower.cpp

TARGET := pavicc

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(SOURCES)

clean:
	rm -f $(TARGET)
