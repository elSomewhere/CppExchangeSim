# Makefile for Exchange Simulation WebAssembly Build
# Requires Emscripten SDK to be installed and activated

# Emscripten compiler
CXX = em++

# Project name
PROJECT = exchange_simulation

# Source directories
SRC_DIR = src
WASM_SRC = wasm_main.cpp

# Output files
WASM_OUTPUT = $(PROJECT).js
WASM_WASM = $(PROJECT).wasm

# Include directories
INCLUDES = -I.

# Compiler flags
CXXFLAGS = -std=c++17 -O2 -DNDEBUG
CXXFLAGS += -Wall -Wextra -Wno-unused-parameter

# Emscripten-specific flags
EMFLAGS = -s WASM=1
EMFLAGS += -s MODULARIZE=1
EMFLAGS += -s EXPORT_NAME='ExchangeSimulationModule'
EMFLAGS += -s EXPORTED_RUNTIME_METHODS='["addOnPostRun"]'
EMFLAGS += -s ALLOW_MEMORY_GROWTH=1
EMFLAGS += -s INITIAL_MEMORY=33554432
EMFLAGS += -s STACK_SIZE=8388608
EMFLAGS += -s DISABLE_EXCEPTION_CATCHING=0
EMFLAGS += -s ASSERTIONS=0
EMFLAGS += -s ASYNCIFY=1
EMFLAGS += --bind

# Source files (header-only implementations, so we only need the main file)
SOURCES = $(WASM_SRC)

# Header dependencies (all headers are included but not compiled separately as they are header-only)
HEADERS = $(wildcard $(SRC_DIR)/*.h) L2WasmHook.h ZeroIntelligenceMarketMaker.h

# Default target
all: $(WASM_OUTPUT)

# Main WebAssembly build
$(WASM_OUTPUT): $(SOURCES) $(HEADERS)
	@echo "Compiling $(PROJECT) to WebAssembly..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(EMFLAGS) $(SOURCES) -o $(WASM_OUTPUT)
	@echo "Build complete: $(WASM_OUTPUT) and $(WASM_WASM)"

# Debug build
debug: CXXFLAGS = -std=c++17 -O0 -g -DDEBUG
debug: EMFLAGS += -s ASSERTIONS=2 -s SAFE_HEAP=1 -s STACK_OVERFLOW_CHECK=2
debug: $(WASM_OUTPUT)

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(WASM_OUTPUT) $(WASM_WASM)
	@echo "Clean complete."

# Install/check Emscripten
check-emscripten:
	@echo "Checking Emscripten installation..."
	@which emcc > /dev/null || (echo "Error: Emscripten not found. Please install and activate Emscripten SDK." && exit 1)
	@echo "Emscripten found: $$(emcc --version | head -n1)"

# Development server (requires Python)
serve: $(WASM_OUTPUT)
	@echo "Starting development server on http://localhost:8000"
	@echo "Press Ctrl+C to stop."
	python3 -m http.server 8000

# Help
help:
	@echo "Exchange Simulation WebAssembly Build"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build WebAssembly module (default)"
	@echo "  debug        - Build debug version with extra checks"
	@echo "  clean        - Remove build artifacts"
	@echo "  check-emscripten - Check if Emscripten is properly installed"
	@echo "  serve        - Start local development server"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Requirements:"
	@echo "  - Emscripten SDK installed and activated"
	@echo "  - C++17 compatible compiler"
	@echo ""
	@echo "Usage:"
	@echo "  make              # Build the project"
	@echo "  make debug        # Build debug version"
	@echo "  make serve        # Build and serve on localhost:8000"

.PHONY: all debug clean check-emscripten serve help 