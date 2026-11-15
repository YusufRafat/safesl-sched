CXX = g++
CXXFLAGS = -O2 -Wall -std=c++17
LDFLAGS = -lbpf -lelf -lz
TARGET = safesl_daemon
SRC = ringbuf_serializer.cpp

all: $(TARGET)

$(TARGET): $(SRC) ../bpf/trace.h
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET) dataset.csv
