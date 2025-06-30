TARGET1=server
#TARGET2=client
CC?=clang
CFLAGS=-I./lib -Wall -Wextra -Wpedantic
CFILES1=src/server.c
#CFILES2=src/client.c

default:
	$(CC) $(CFLAGS) -o $(TARGET1) $(CFILES1)
#	$(CC) $(CFLAGS) -o $(TARGET2) $(CFILES2)
clean:
	rm -f server
#	rm -f client
debug:
	$(CC) $(CFLAGS) -o $(TARGET1) -g $(CFILES1)
#	$(CC) $(CFLAGS) -o $(TARGET2) -g $(CFILES2)
all: default
