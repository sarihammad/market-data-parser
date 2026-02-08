# Makefile for Fast Market Parser
# High-performance build configuration

CXX = g++
CXXFLAGS = -std=c++20 -Iinclude -pthread

# Release build flags (maximum optimization)
RELEASE_FLAGS = -O3 -march=native -mtune=native -DNDEBUG
RELEASE_FLAGS += -funroll-loops -finline-functions -ffast-math -ftree-vectorize
RELEASE_FLAGS += -flto

# Debug build flags
DEBUG_FLAGS = -O0 -g -Wall -Wextra -Wpedantic
DEBUG_FLAGS += -fsanitize=address -fsanitize=undefined

# Default to release build
BUILD_TYPE ?= release

ifeq ($(BUILD_TYPE),debug)
    CXXFLAGS += $(DEBUG_FLAGS)
else
    CXXFLAGS += $(RELEASE_FLAGS)
endif

# Source files
SRC_DIR = src
TEST_DIR = test
BUILD_DIR = build

# Object files
SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

# Executables
BENCHMARK = $(BUILD_DIR)/parser_benchmark
DEMO = $(BUILD_DIR)/parser_demo
TEST = $(BUILD_DIR)/parser_test

.PHONY: all clean test demo benchmark directories

all: directories $(TEST) $(DEMO) $(BENCHMARK)

directories:
	@mkdir -p $(BUILD_DIR)

# Build object files (excluding main files)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# Build test executable
$(TEST): $(TEST_DIR)/test_parser.cpp $(BUILD_DIR)/itch_parser.o $(BUILD_DIR)/async_logger.o $(BUILD_DIR)/mpmc_queue.o $(BUILD_DIR)/system_utils.o
	@echo "Building test executable..."
	@$(CXX) $(CXXFLAGS) $^ -o $@

# Build demo executable
$(DEMO): $(SRC_DIR)/demo.cpp $(BUILD_DIR)/itch_parser.o $(BUILD_DIR)/async_logger.o $(BUILD_DIR)/mpmc_queue.o $(BUILD_DIR)/system_utils.o
	@echo "Building demo executable..."
	@$(CXX) $(CXXFLAGS) $^ -o $@

# Build benchmark executable
$(BENCHMARK): $(SRC_DIR)/benchmark.cpp $(BUILD_DIR)/itch_parser.o $(BUILD_DIR)/async_logger.o $(BUILD_DIR)/mpmc_queue.o $(BUILD_DIR)/system_utils.o
	@echo "Building benchmark executable..."
	@$(CXX) $(CXXFLAGS) $^ -o $@

test: $(TEST)
	@echo ""
	@echo "Running tests..."
	@$(TEST)

demo: $(DEMO)
	@echo ""
	@echo "Running demo..."
	@$(DEMO)

benchmark: $(BENCHMARK)
	@echo ""
	@echo "Running benchmark..."
	@$(BENCHMARK)

clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -f *.bin

help:
	@echo "Fast Market Data Parser - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build all executables (default)"
	@echo "  test       - Build and run tests"
	@echo "  demo       - Build and run demo"
	@echo "  benchmark  - Build and run benchmark"
	@echo "  clean      - Remove build artifacts"
	@echo ""
	@echo "Build modes:"
	@echo "  make BUILD_TYPE=release  - Optimized build (default)"
	@echo "  make BUILD_TYPE=debug    - Debug build with sanitizers"
	@echo ""
	@echo "Examples:"
	@echo "  make              # Build everything (release mode)"
	@echo "  make test         # Run unit tests"
	@echo "  make benchmark    # Run performance benchmark"
	@echo "  make clean all    # Clean rebuild"
