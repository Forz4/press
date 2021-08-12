INCPATH = include
LIBPATH = ../lib
BINPATH = ../bin
LIBS = -lhiredis
CFLAGS = -Wall -std=gnu99 -fPIC -g
CC = @gcc
MV = @mv
AR = @ar -ruc
RM = @-rm -f
ECHO = @echo

all: press presscmd

.c.o:
	$(CC) -c $< -I$(INCPATH) $(CFLAGS)

presscmd: presscmd.o log.o pubfunc.o 8583.o
	$(CC) -o $@ $^ $(LIBS) -L$(LIBPATH)
	$(MV) $@ $(BINPATH)

press: press.o log.o pubfunc.o 8583.o
	$(CC) -o $@ $^ $(LIBS) -L$(LIBPATH)
	$(MV) $@ $(BINPATH)

clean:
	$(RM) *.o
	$(RM) $(BINPATH)/press $(BINPATH)/presscmd 
