CC=gcc
#DEBUG=-ggdb3 -D_DEBUG -Wpadded
#DEBUG=-Wpadded
FLAGS=$(DEBUG) -Wall -O2

INCLUDES=$(shell pkg-config --cflags libusb-1.0)
LIBS=-lpthread $(shell pkg-config --libs libusb-1.0)
LIB_DIRS=

COMPILE=$(CC) $(INCLUDES) $(FLAGS) -c

EXE=midex

SOURCES=main.c \
        thread_ep2.c \
        thread_ep4.c \
        thread_ep6.c

OBJECTS := $(SOURCES:%.c=%.o)

all: $(EXE)

$(EXE): $(OBJECTS)
	$(CC) -o $(EXE) $(OBJECTS) $(LIBS) $(LIB_DIRS)

%.o: %.c
	$(COMPILE) $< -o $@ 

.PHONY: clean
clean: 
	rm -rf $(OBJECTS) $(EXE) *~


