# Simple Makefile for standalone bag processor
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
TARGET = standalone_bag_processor
SOURCE = standalone_bag_processor.cpp

# Default target
$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE)

# Clean target
clean:
	rm -f $(TARGET)

# Install target (copy to build directory)
install: $(TARGET)
	mkdir -p build
	cp $(TARGET) build/

.PHONY: clean install