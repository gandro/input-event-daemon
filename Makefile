all: input-event-daemon docs/input-event-daemon.8 docs/input-event-daemon.html

input-event-daemon: input-event-daemon.c input-event-daemon.h input-event-table.h
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

input-event-table.h: /usr/include/linux/input.h
	awk -f parse_input_h.awk < $< > $@

docs/input-event-daemon.8: docs/input-event-daemon.txt
	a2x -f manpage $<

docs/input-event-daemon.html: docs/input-event-daemon.txt
	asciidoc $<

clean:
	rm -f input-event-daemon

install:
	install -D -m 755 input-event-daemon $(DESTDIR)/usr/bin
	install -D -m 644 docs/input-event-daemon.8 $(DESTDIR)/usr/share/man/man8/
	install -D -b -m 644 docs/sample.conf $(DESTDIR)/etc/input-event-daemon.conf

uninstall:
	rm -f $(DESTDIR)/usr/bin/input-event-daemon
	rm -f $(DESTDIR)/usr/share/man/man8/input-event-daemon.8
	rm -f $(DESTDIR)/etc/input-event-daemon.conf
