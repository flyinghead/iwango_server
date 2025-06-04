#
# build dependencies: libasio-dev libicu-dev libcurl-dev libsqlite3-dev
# runtime dependencies: fcgiwrap
#
prefix = /usr/local
exec_prefix = $(prefix)
sbindir = $(exec_prefix)/sbin
bindir = $(exec_prefix)/bin
sysconfdir = $(prefix)/etc
libexecdir = $(exec_prefix)/libexec
CXXFLAGS=-std=c++17 -g -O3 -Wall -DNDEBUG # -fsanitize=address -static-libasan
DEPS=asio.h database.h models.h lobby_server.h gate_server.h common.h vms.h sega_crypto.h json.hpp discord.h
USER=dcnet

all: iwango_server keycutter keycutter.cgi culdcept-gamedata

iwango_server: lobby_server.o models.o packet_processor.o gate_server.o database.o discord.o common.o
	$(CXX) $(CXXFLAGS) -o $@ lobby_server.o models.o packet_processor.o gate_server.o database.o discord.o common.o -lpthread -licuuc -lcurl -lsqlite3

keycutter: keycutter.o sega_crypto.o
	$(CXX) $(CXXFLAGS) -o keycutter keycutter.o sega_crypto.o

keycutter.cgi: sega_crypto.o keycutter-cgi.o
	$(CXX) $(CXXFLAGS) -o keycutter.cgi sega_crypto.o keycutter-cgi.o

culdcept-gamedata: culdcept-gamedata.o
	$(CXX) $(CXXFLAGS) -o culdcept-gamedata culdcept-gamedata.o

%.o: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f *.o iwango_server keycutter keycutter.cgi culdcept-gamedata

install: iwango_server keycutter.cgi
	mkdir -p $(DESTDIR)$(sbindir)
	install iwango_server $(DESTDIR)$(sbindir)
	mkdir -p $(DESTDIR)$(libexecdir)/cgi-bin
	install keycutter.cgi $(DESTDIR)$(libexecdir)/cgi-bin
	mkdir -p $(DESTDIR)$(sysconfdir)
	cp -n iwango.cfg $(DESTDIR)$(sysconfdir)

iwango.service: iwango.service.in Makefile
	sed -e "s/INSTALL_USER/$(USER)/g" -e "s:SBINDIR:$(sbindir):g" -e "s:SYSCONFDIR:$(sysconfdir):g" < $< > $@

installservice: iwango.service
	mkdir -p /usr/lib/systemd/system/
	cp $< /usr/lib/systemd/system/
	systemctl enable $<

createdb: iwango.sql
	mkdir -p /var/lib/iwango
	sqlite3 /var/lib/iwango/iwango.db < iwango.sql
	chown $(USER):$(USER) /var/lib/iwango /var/lib/iwango/iwango.db
