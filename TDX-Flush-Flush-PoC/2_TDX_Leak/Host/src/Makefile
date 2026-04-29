CXX = g++
CXXFLAGS = -O2 -std=c++17 -pthread
TARGET = host_spy_ff
SRC = host_spy_ff.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
