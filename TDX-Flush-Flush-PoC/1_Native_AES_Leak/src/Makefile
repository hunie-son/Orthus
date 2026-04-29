CC      := gcc
CFLAGS  := -O2 -std=c11 -I./openssl-1.1.1w/include
LDFLAGS := -L./openssl -Wl,-rpath,'$$ORIGIN/openssl' -lcrypto

TARGET  := aes_ff_spy_single_v2
SRC     := aes_ff_spy_single_v2.c
HDR     := ff_utils.h


#TARGET  := aes_ff_spy
#SRC     := aes_ff_spy.c
#HDR     := ff_utils.h


.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

