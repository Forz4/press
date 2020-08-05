CCFLAG=-Wall -std=gnu99
CC=gcc

#CCFLAG=-g
#CC=cc

all: press presscmd

presscmd: presscmd.o log.o pubfunc.o
	@$(CC) -o presscmd presscmd.o log.o pubfunc.o
	@mv presscmd ../bin/

press: press.o log.o pubfunc.o
	@$(CC) -o press press.o log.o pubfunc.o
	@mv press ../bin/

log.o: log.c include/log.h
	@$(CC) -c log.c $(CCFLAG)

pubfunc.o: pubfunc.c include/pubfunc.h
	@$(CC) -c pubfunc.c $(CCFLAG)

press.o: press.c include/press.h
	@$(CC) -c press.c $(CCFLAG)

presscmd.o: presscmd.c include/presscmd.h
	@$(CC) -c presscmd.c $(CCFLAG)

clean:
	@-rm -f *.o

