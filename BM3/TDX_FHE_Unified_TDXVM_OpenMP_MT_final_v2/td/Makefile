CXX=g++
CXXFLAGS=-std=c++17 -O2 \
    -I$(HOME)/openssl-local/include \
    -I$(HOME)/SEAL/SH_install_host/include/SEAL-4.1
LDFLAGS=-L$(HOME)/openssl-local/lib64 -lcrypto \
        -L$(HOME)/SEAL/SH_install_host/lib -lseal-4.1

all: td-init td-run

td-init: td-init.o
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

td-run: td-run.o
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

%.o: %.cpp utils.hpp
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f *.o td-init td-run
.PHONY: all clean
