CXX = g++
CXXFLAGS = -Wall -g

SERVER_SRC = server.cpp
SUBSCRIBER_SRC = subscriber.cpp

SERVER_BIN = server
SUBSCRIBER_BIN = subscriber

.PHONY: all clean

all: $(SERVER_BIN) $(SUBSCRIBER_BIN)

$(SERVER_BIN): $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(SUBSCRIBER_BIN): $(SUBSCRIBER_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(SERVER_BIN) $(SUBSCRIBER_BIN)
