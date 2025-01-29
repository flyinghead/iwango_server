CXXFLAGS=-std=c++17 -g -Wall -fsanitize=address
DEPS=database.h models.h lobby_server.h gate_server.h common.h vms.h

all: iwango_server keycutter

iwango_server: lobby_server.o models.o packet_processor.o gate_server.o 
	$(CXX) $(CXXFLAGS) -o $@ lobby_server.o models.o packet_processor.o gate_server.o -lpthread

keycutter: sega_crypto.o
	$(CXX) $(CXXFLAGS) -o keycutter sega_crypto.o

%.o: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f *.o iwango_server keycutter
