BIN=rb_get_raw
CC=gcc
SRCS=rb_get_raw.c enrichment.c util.c

$(BIN): rb_get_raw.o enrichment.o util.o
	gcc $(SRCS) -L /opt/rb/lib -I /opt/rb/include/ -lyajl -lcurl -ludns -o $(BIN)

rb_get_raw.o: rb_get_raw.c
	gcc -c -L /opt/rb/lib -I /opt/rb/include/ rb_get_raw.c

enrichment.o: enrichment.o
	gcc -c -L /opt/rb/lib -I /opt/rb/include/ enrichment.c

util.o: util.c
	gcc -c -L /opt/rb/lib -I /opt/rb/include/ util.c

install: $(BIN)
	cp $(BIN) /opt/rb/bin

clean:
	rm -f *.o
