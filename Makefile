CXX=g++
CXXFLAGS=-g
LIBS=-lncurses -lsocket -lnsl
INCS=-I/usr/local/include/ncurses

all: client server

clean:
	rm -f client server

.PHONY: all clean

client: client.cpp
	$(CXX) client.cpp $(CXXFLAGS) $(INCS) $(LIBS) -o client

server: server.cpp
	$(CXX) server.cpp $(CXXFLAGS) $(INCS) $(LIBS) -o server
