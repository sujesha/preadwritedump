#pdatadump Makefile has this command, so add changes there also!

CC  = gcc
CFLAGS  = -Wall -O2 -g -W
ALL_CFLAGS = $(CFLAGS) $(EXTRA_CFLAGS) -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -DDEBUG_SS
LIBS    = -lpthread

default:
	rm -rf *.o psiphon
	$(CC) $(ALL_CFLAGS) -I../ -o psiphon endianness.c recv_kernel_sig.c main_psiphon.c $(LIBS)

psiphon: 
	rm -rf *.o psiphon
	$(CC) $(ALL_CFLAGS) -I../ -o psiphon endianness.c recv_kernel_sig.c main_psiphon.c $(LIBS)
