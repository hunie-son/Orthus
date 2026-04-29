CXX = g++
# Ensure -I points to where you unpacked openssl headers in guest
CXXFLAGS = -O2 -std=c++17 -I/root/openssl-1.1.1w/include
# Ensure -L points to libcrypto compiled location
LDFLAGS = -L/root/openssl-1.1.1w -Wl,-rpath,/root/openssl-1.1.1w -lcrypto

TARGET = guest_te0_ff_worker
SRC = guest_te0_ff_worker.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)
