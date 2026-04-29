CXX := g++
CXXFLAGS := -std=c++17 -O2 -fopenmp \
  -I/usr/local/openfhe/include \
  -I/usr/local/openfhe/include/openfhe \
  -I/usr/local/openfhe/include/openfhe/core \
  -I/usr/local/openfhe/include/openfhe/pke \
  -I/usr/local/openfhe/include/openfhe/binfhe
LDFLAGS := -L/usr/local/openfhe/lib -lOPENFHEpke -lOPENFHEcore -lOPENFHEbinfhe -lpthread -ldl -fopenmp

all: host2-run

host2-run: host2-run.o
	$(CXX) -o $@ $^ $(LDFLAGS)

host2-run.o: host2-run.cpp utils.hpp
	$(CXX) $(CXXFLAGS) -c host2-run.cpp

clean:
	rm -f host2-run.o host2-run

.PHONY: all clean
