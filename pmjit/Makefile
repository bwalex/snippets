CFLAGS= -O0 -g3
LIBS=	-ldwarf
LDFLAGS=	-O0 -g3

OBJS=	test.o pmjit.o dyn_array.o pmjit-gdb.o
X86OBJS=target-x86_64.o

WARNFLAGS=-Wsystem-headers -Wall -W \
	  -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith \
	  -Wold-style-definition -Wreturn-type -Wwrite-strings \
	  -Wswitch -Wshadow -Wcast-align -Wchar-subscripts \
	  -Wnested-externs \
	  -Wno-unused-variable -Wno-unused-parameter


CFLAGS+= $(WARNFLAGS)

test:	$(OBJS) $(X86OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

clean:
	rm -f $(OBJS)
	rm -f $(X86OBJS)
	rm -f test
