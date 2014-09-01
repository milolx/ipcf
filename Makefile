SRCS    := $(wildcard *.c)
SRCS    := $(SRCS) $(wildcard lib/*.c)
SRCS    := $(SRCS) $(wildcard sys/*.c)
OBJS    := $(patsubst %.c, %.o, $(SRCS))
HDRS	:= $(wildcard include/*.h)
HDRS	:= $(HDRS) $(wildcard *.h)
TARGET  := ipcf
WARN    := -Wall
INCLUDE := -I./include
LIBRARY := -L./lib
DEBUG   :=  -D__DEBUG__
CFLAGS  := -O2  ${WARN} ${INCLUDE}
LDFLAGS := ${LIBRARY} -lpthread -lrt
#CC      := powerpc-gcc
CC      := gcc

all: ${TARGET} tags

${TARGET}: ${OBJS}
	${CC}  ${LDFLAGS}  -o $@ ${OBJS}

%.o: %.c
	${CC} ${CFLAGS} ${DEBUG} ${SSL} -c -o $@ $<

tags: ${SRCS} ${HDRS} 
	ctags ${SRCS} ${HDRS}

.PHONY: clean
clean:
	rm -rf ${TARGET} ${OBJS} tags
