
TARGET = libwtinfo.so wtinfo wtinfo.so

all: $(TARGET)

%.lo: %.c
	$(CC) -fpic $(CFLAGS) -c -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

libwtinfo.so: wtinfo.lo
	$(CC) $(LDFLAGS) -shared -o $@ $^ -lcjson -lcrypto -lm

wtinfo: cli.o
	$(CC) -o $@ $^ -L. $(LDFLAGS) -lwtinfo -lcjson -lcrypto -lm

wtinfo.so: libwtinfo.so wtinfo_lua.lo
	$(CC) -o $@ -shared $^ $(LDFLAGS) -shared -L. -lwtinfo -lcjson -lcrypto -lm -llua

.PHONY: clean

clean:
	rm -rf *.o *.lo $(TARGET)

