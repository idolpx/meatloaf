# Makefile for retropixels-cpp
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I./include -I./src
LDFLAGS =

# Target executable
TARGET = retropixels
OUTDIR = bin
OBJDIR = obj

# Source files
SRC_DIR = src
SOURCES = $(wildcard $(SRC_DIR)/**/*.cpp $(SRC_DIR)/*.cpp)

# Object files
OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

# Ensure we have all required directories
DIRS = $(OBJDIR) $(OBJDIR)/conversion $(OBJDIR)/io $(OBJDIR)/model \
       $(OBJDIR)/prepost $(OBJDIR)/profiles $(OUTDIR)

.PHONY: all clean install test

all: $(OUTDIR)/$(TARGET)

# Create necessary directories
$(DIRS):
	mkdir -p $@

# Build target executable
$(OUTDIR)/$(TARGET): $(OBJECTS) | $(OUTDIR)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)
	@echo "Built $(TARGET) successfully!"

# Compile source files to object files
$(OBJDIR)/%.o: $(SRC_DIR)/%.cpp | $(DIRS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR)
	rm -f $(OUTDIR)/$(TARGET)
	rm paintface.kla
	@echo "Cleaned build artifacts (preserved viewer files)"

install: all
	cp $(OUTDIR)/$(TARGET) /usr/local/bin/
	mkdir -p /usr/local/share/retropixels/target/c64
	cp cli/target/c64/*.prg /usr/local/share/retropixels/target/c64/
	@echo "Installed $(TARGET) to /usr/local/bin/"
	@echo "Installed viewer files to /usr/local/share/retropixels/target/c64/"

# Test by converting a sample image
test: all
	@echo "Testing $(TARGET)..."
	@if [ -f "paintface.jpg" ]; then \
		$(OUTDIR)/$(TARGET) -i paintface.jpg -o paintface.kla --overwrite; \
		echo "Test completed - output: paintface.kla"; \
	else \
		echo "Warning: No test image found at 'paintface.jpg'"; \
	fi

# Print variables for debugging
debug:
	@echo "SOURCES: $(SOURCES)"
	@echo "OBJECTS: $(OBJECTS)"
	@echo "DIRS: $(DIRS)"
