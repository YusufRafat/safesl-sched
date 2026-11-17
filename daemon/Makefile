CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11
LDFLAGS = -lbpf -lelf -lz

TARGET = safesl_daemon
SRC = ringbuf_serializer.c

all: $(TARGET)

$(TARGET): $(SRC) ../bpf/trace.h
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET) dataset.csv
