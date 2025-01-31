#
# dependencies: libasio-dev libicu-dev
#
CXXFLAGS=-std=c++17 -O3 -Wall # -fsanitize=address -static-libasan
DEPS=asio.h database.h models.h lobby_server.h gate_server.h common.h vms.h sega_crypto.h
INSTALL_DIR=/usr/local
USER=dcnet

all: iwango_server keycutter keycutter.cgi

iwango_server: lobby_server.o models.o packet_processor.o gate_server.o 
	$(CXX) $(CXXFLAGS) -o $@ lobby_server.o models.o packet_processor.o gate_server.o -lpthread -licuuc

keycutter: keycutter.o sega_crypto.o
	$(CXX) $(CXXFLAGS) -o keycutter keycutter.o sega_crypto.o

keycutter.cgi: sega_crypto.o keycutter-cgi.o
	$(CXX) $(CXXFLAGS) -o keycutter.cgi sega_crypto.o keycutter-cgi.o

%.o: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f *.o iwango_server keycutter keycutter.cgi

install: iwango_server keycutter.cgi
	install iwango_server "$(INSTALL_DIR)/bin"
	install keycutter.cgi "$(INSTALL_DIR)/lib/cgi-bin"

iwango.service: iwango.service.in Makefile
	sed -e "s/INSTALL_USER/$(USER)/g" -e "s:INSTALL_DIR:$(INSTALL_DIR):g" < $< > $@

installservice: iwango.service
	install -m 0644 $< $(INSTALL_DIR)/lib/systemd
	systemctl enable $(INSTALL_DIR)/lib/systemd/$<
