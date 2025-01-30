CXXFLAGS=-std=c++17 -O3 -Wall #-fsanitize=address -static-libasan
DEPS=database.h models.h lobby_server.h gate_server.h common.h vms.h sega_crypto.h

all: iwango_server keycutter keycutter.cgi

iwango_server: lobby_server.o models.o packet_processor.o gate_server.o 
	$(CXX) $(CXXFLAGS) -o $@ lobby_server.o models.o packet_processor.o gate_server.o -lpthread

keycutter: keycutter.o sega_crypto.o
	$(CXX) $(CXXFLAGS) -o keycutter keycutter.o sega_crypto.o

keycutter.cgi: sega_crypto.o keycutter-cgi.o
	$(CXX) $(CXXFLAGS) -o keycutter.cgi sega_crypto.o keycutter-cgi.o

%.o: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f *.o iwango_server keycutter keycutter.cgi
