CPPFLAGS=-O2 -MMD -I /usr/include/g++-3/
LDFLAGS=
ifeq (${OS},Windows_NT)
CYGLIB=/lib/w32api/libwsock32.a
else
CYGLIB=
endif

all:		fecsend fecrecv fectest

%.d:	%.c
	$(CC) $(CPPFLAGS) -c -o $*.o $<

CSOURCES=$(wildcard *.c)

include $(CSOURCES:.c=.d)

fecsend:	fecsend.o fec.o ${CYGLIB}

fecrecv:	fecrecv.o fec.o ${CYGLIB}

fectest:	fectest.o fec.o

clean:
		rm -rf *~ *.bak *.d DEADJOE `find -name "*.o"` core *.exe \
		fectest fecsend fecrecv *.stackdump \
		Debug/ Release/ *.dsw *.ncb *.opt *.plg x.fec

updateweb:
		 tar cjf - index.html documentation.html | ssh -l nroets \
      feclib.sourceforge.net 'cd /home/groups/f/fe/feclib/htdocs; tar xjf -'
