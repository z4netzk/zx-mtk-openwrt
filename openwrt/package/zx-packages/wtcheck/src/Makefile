CFLAGS += -std=gnu99 -fstrict-aliasing -Iinclude -Wall -Werror
CFLAGS += -DRSA_VERIFY

all: wtcheck

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

wtcheck: main.o crc.o sha1.o rsa_verify.o
	$(CC) $(LDFLAGS) -o $@ $^

.PHONY: clean

clean:
	rm -rf *.o wtcheck
