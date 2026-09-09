# GIS Cup 2026 antenna placement solver.
#
#   make            optimised build with OpenMP (default)
#   make portable   drops -march=native, for running on a different CPU than the builder
#   make debug      assertions plus sanitizers
#   make test       build and run the self-tests

CXX      ?= g++
STD      := -std=c++17
WARN     := -Wall -Wextra -Wno-unused-parameter
OPT      := -O3 -march=native -funroll-loops -fno-math-errno
OMP      := -fopenmp

SRCDIR   := src
BINDIR   := bin
OBJDIR   := build

SOURCES  := $(wildcard $(SRCDIR)/*.cpp)
OBJECTS  := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))
TARGET   := $(BINDIR)/gis_cup

CXXFLAGS := $(STD) $(WARN) $(OPT) $(OMP)
LDFLAGS  := $(OMP)

.PHONY: all portable debug test clean

all: $(TARGET)

portable: OPT := -O3 -mavx2 -mfma -funroll-loops -fno-math-errno
portable: clean $(TARGET)

debug: OPT := -O1 -g -fsanitize=address,undefined
debug: LDFLAGS := $(OMP) -fsanitize=address,undefined
debug: clean $(TARGET)

$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BINDIR) $(OBJDIR):
	@mkdir -p $@

test: $(TARGET)
	$(TARGET) selftest

clean:
	@rm -rf $(OBJDIR) $(TARGET)

-include $(wildcard $(OBJDIR)/*.d)
