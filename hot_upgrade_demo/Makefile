CXX=g++
LDFLAGS=-lpthread -lsnappy
CXXFLAGS=-std=c++11 -g

.PHONY : clean all

INCLUDE_PATH = -I./

OBJECT : server client

all: $(OBJECT)

server: server.cc epoll.cc inet_socket.cc inet_conn.cc unix_socket.cc unix_conn.cc unix_client.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@  $(INCLUDE_PATH)

client: client.cc inet_socket.cc
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@  $(INCLUDE_PATH)

clean:
	rm -rf server client
