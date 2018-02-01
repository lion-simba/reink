CFLAGS= -c

all: reink

SOURCES := reink.c d4lib.c printers.c util.c
OBJS := $(SOURCES:.c=.o)

reink: ${OBJS}
	$(CC) $^ -o $@

printers.c: printers.h
reink.c: printers.h d4lib.h util.h
d4lib.c: d4lib.h util.h
util.c: util.h

clean:
	rm -f reink ${OBJS}
