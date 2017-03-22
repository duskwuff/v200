CC = gcc
CFLAGS = -Wall -ggdb
LDLIBS =

CFLAGS += $(shell sdl2-config --cflags)
LDLIBS += $(shell sdl2-config --libs)

MUSASHI_C += m68kcpu.c
MUSASHI_C += $(MUSASHI_GEN_C)
MUSASHI_GEN_C = m68kops.c m68kopac.c m68kopdm.c m68kopnz.c
MUSASHI_GEN_H = m68kops.h
MUSASHI_O = $(MUSASHI_C:.c=.o)

BINARY = v200
OBJECTS += v200.o
OBJECTS += $(MUSASHI_O)

$(BINARY): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDLIBS) $(OBJECTS) -o $(BINARY)

v200.o: v200.c m68kops.h

clean:
	rm -f $(BINARY) $(OBJECTS) \
	    $(MUSASHI_GEN_C) $(MUSASHI_GEN_H) \
	    m68kmake m68kmake.o

m68kmake: m68kmake.o

$(MUSASHI_GEN_C) $(MUSASHI_GEN_H): m68kmake
	./m68kmake .
