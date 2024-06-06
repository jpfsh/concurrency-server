CC=gcc
CFLAGS=-Iinclude -Wall -Werror -g -Wno-unused 

DEPS=$(shell find include -name '*.h')
SRC_DIR:=src
LIBS=-lpthread

all: setup MTserver RWserver

setup:
	mkdir -p bin 

MTserver: setup 
	$(CC) $(CFLAGS) $(SRC_DIR)/dlinkedlist.c $(SRC_DIR)/MThelpers.c $(SRC_DIR)/MTserver.c -o bin/ZotDonate_MTserver $(LIBS)

RWserver: setup 
	$(CC) $(CFLAGS) $(SRC_DIR)/dlinkedlist.c $(SRC_DIR)/RWhelpers.c $(SRC_DIR)/RWserver.c -o bin/ZotDonate_RWserver $(LIBS)

.PHONY: clean

clean:
	rm -rf bin 
