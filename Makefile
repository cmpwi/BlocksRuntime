
CFLAGS = -std=c23 -fPIC -Wall -Werror -Os
LDFLAGS = -shared

SRCS = BlocksRuntime.c BlocksRuntimeExtra.c
HEADERS = ../Block.h ../Block_private.h
OBJS = ${SRCS:S/.c/.o/g}
OBJDIR = obj
TARGET = libBlocksRuntime.so

STRIP = strip --strip-all

all: ${TARGET}

obj:
	mkdir -p ${OBJDIR}

.c.o:
	${CC} ${CFLAGS} -c ${.IMPSRC} -o ${.TARGET}

${TARGET}: ${OBJS}
	${CC} ${LDFLAGS} -o ${.TARGET} ${OBJS}
	${STRIP} ${TARGET}

clean:
	rm -rf ../obj

install:
	install -o root -g bin -m 444 ${TARGET} /usr/lib
	install -o root -g bin -m 444 ${HEADERS} /usr/include

.PHONY: all clean install
