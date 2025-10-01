# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++20 -Wall -I/usr/include -O2

# Libraries (Boost, pthread)
LDLIBS = -lboost_system -lboost_json -lpthread

# Source and target
SRC = communication.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = communication

# Default rule
all: $(TARGET)

# Build executable
$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

# Compile object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build files
clean:
	rm -f $(OBJ) $(TARGET)
