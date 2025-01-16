CC=gcc
CFLAGS=
WFLAGS=-Wall -Werror
ifeq ($(shell uname -s),Darwin)
	WFLAGS=-Wall
endif

wt-objs = wtsign.o

all: wtsign

%.o: %.c
	$(CC) $(CFLAGS) $(WFLAGS) -c -o $@ $<

wtsign: $(wt-objs)
	$(CC) -o $@ $? $(LDFLAGS)

clean:
	-rm -f *.o wtsign
