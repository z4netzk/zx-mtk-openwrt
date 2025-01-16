
TARGET = wtoem

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^

wtoem: cli.o wtoem.o
	$(CC) -o $@ $^ -L. $(LDFLAGS)

.PHONY: clean

clean:
	rm -rf *.o *.lo $(TARGET)

