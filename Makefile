
CC = gcc

DEFINES = _GNU_SOURCE

LIBFLAGS = -shared
CWARN = -Wall
COPT = -O0
ifneq ($(DEBUG),)
	CDEBUG = -ggdb3
else
	CDEBUG =
endif

CDEFINES = $(foreach d,$(DEFINES),-D$(d))

CFLAGS = $(CWARN) $(COPT) $(CDEFINES) $(LIBFLAGS) $(CDEBUG)

EXTRAFLAGS = $(shell pkg-config --cflags --libs fuse bash)

booze.so: booze.c
	$(CC) $(CFLAGS) $(EXTRAFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f booze.so
