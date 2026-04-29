CXX        := g++
OPENFHE_INC:= /usr/local/openfhe/include
OPENFHE_LIB:= /usr/local/openfhe/lib
CXXFLAGS   := -std=c++17 -O2 \
  -I$(OPENFHE_INC) \
  -I$(OPENFHE_INC)/openfhe \
  -I$(OPENFHE_INC)/openfhe/core \
  -I$(OPENFHE_INC)/openfhe/pke  \
  -I$(OPENFHE_INC)/openfhe/binfhe
LDFLAGS    := -L$(OPENFHE_LIB) -lOPENFHEpke -lOPENFHEcore -lOPENFHEbinfhe -lpthread -ldl

all: host1-run host1-decrypt

host1-run: main.o model.o data.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

host1-decrypt: host1-decrypt.o data.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $<

clean:
	rm -f *.o host1-run host1-decrypt

.PHONY: all clean
