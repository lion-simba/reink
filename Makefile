CFLAGS= -c

all: reink

reink: reink.o d4lib.o printers.o
	$(CC) $^ -o $@

printers.o: printers.c printers.h
	$(CC) $(CFLAGS) printers.c -o $@
    
reink.o: reink.c printers.h d4lib.h
	$(CC) $(CFLAGS) reink.c -o $@
    
d4lib.o: d4lib.c d4lib.h
	$(CC) $(CFLAGS) d4lib.c -o $@

clean:
	rm -f reink reink.o d4lib.o printers.o
    