LXINPUT=/usr/include/linux/input.h

all: input-event-daemon

input-event-daemon: input-event-daemon.c input-event-table.h
	$(CC) $(CFLAGS) $+ $(LDFLAGS) -o $@ -Wall

input-event-table.h:
	awk -f parse_input_h.awk < $(LXINPUT) > $@
