# Makefile for CS3102 Practical P2: Simple, Reliable Transport Protocol (SRTP)
#
# Saleem Bhatti <https://saleem.host.cs.st-andrews.ac.uk/>
# Jan 2024, Feb 2023, Jan 2022, Jan 2021
# checked March 2025 (sjm55)

# CC =clang
CC	=gcc
CC-flags =-Wall -g #-DOBFUSCATE #-DDEBUG

PROGRAMS = test-client-0 test-server-0 \
			     test-client-1 test-server-1 \
			     test-client-2 test-server-2 \
					 test-client-3 test-server-3

H-files	= srtp.h srtp-common.h srtp-pcb.h byteorder64.h d_print.h

.SUFFIXES:	.h .c .o

.c.o:;	$(CC) $(CC-flags) -c $<

LIB-files	=srtp.o srtp-common.o srtp-pcb.o srtp-fsm.o byteorder64.o d_print.o

all:	$(PROGRAMS) libsrtp.a

libsrtp.a: $(LIB-files)
	ar ruv $@ $+

srtp.c:	$(H-files) srtp-packet.h srtp-fsm.h

srtp-common.c: srtp-common.h

srtp-fsm.c: srtp-fsm.h srtp-fsm.h

srtp-pcb.c: srtp-pcb.h srtp-fsm.h

byteorder64.c:	byteorder64.h

d_print.c:	d_print.h


test-client-0: test-client-0.o libsrtp.a

test-client-0.c: $(H-files)

test-server-0: test-server-0.o libsrtp.a

test-server-0.c: $(H-files)


test-client-1: test-client-1.o libsrtp.a

test-client-1.c: $(H-files)


test-server-1: test-server-1.o libsrtp.a

test-server-1.c: $(H-files)


test-client-2: test-client-2.o libsrtp.a

test-client-2.c: $(H-files)


test-server-2: test-server-2.o libsrtp.a

test-server-2.c: $(H-files)


test-client-3: test-client-3.o libsrtp.a

test-client-3.c: $(H-files)


test-server-3: test-server-3.o libsrtp.a

test-server-3.c: $(H-files)


.PHONY:	clean

clean:;		rm -rf *.o *.a $(PROGRAMS) *~
