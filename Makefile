BIN=	rb_get_raw
CC= gcc
SRCS=	rb_get_raw.c enrichment.c util.c

$(BIN): rb_get_raw.o enrichment.o util.o
	gcc $(SRCS) -lyajl -lcurl -ludns -o $(BIN)

install: $(BIN)
	cp $(BIN) /opt/rb/bin

clean:
	rm -f *.o