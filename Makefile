CXX=g++
CXXFLAGS=-g
LIBS=-lncurses -lsocket -lnsl
INCS=-I/usr/local/include/ncurses

all: client

client: client.cpp
	$(CXX) client.cpp $(CXXFLAGS) $(INCS) $(LIBS) -o client
