BIN=	rb_get_raw

SRCS=	rb_get_raw.c enrichment.c util.c
OBJS=	$(SRCS:.c=.o)

.PHONY:

all: $(BIN)

include mklove/Makefile.base

.PHONY: version.c

version.c:
	@rm -f $@
	@echo "const char *rb_register_revision=\"`git describe --abbrev=6 --dirty --tags --always`\";" >> $@
	@echo 'const char *rb_register_version="1.0.1";' >> $@

install: bin-install

clean: bin-clean

-include $(DEPS)
