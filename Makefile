CFLAGS = -O0 -Wall -W -g
CCOPT = $(CFLAGS)

OBJ = adlist.o ae.o aUnixDomain.o dict.o rcmd.o sds.o
CLIOBJ = aUnixDomain.o adlist.o sds.o rcmd-cli.o

PRGNAME = rcmd-server
CLIPRGNAME = rcmd

all : rcmd-server rcmd

adlist.o : adlist.c adlist.h
ae.o : ae.c ae.h
aUnixDomain.o : aUnixDomain.c aUnixDomain.h
dict.o : dict.c dict.h
sds.o : sds.c sds.h
rcmd.o : rcmd.c ae.h sds.h aUnixDomain.h dict.h adlist.h

rcmd-server : $(OBJ)
	$(CC) -o $(PRGNAME) $(CCOPT) $(OBJ)
rcmd : $(CLIOBJ)
	$(CC) -o $(CLIPRGNAME) $(CCOPT) $(CLIOBJ)

clean :
	rm -fr $(PRGNAME) $(CLIPRGNAME) *.o
