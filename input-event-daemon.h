#ifndef INPUT_EVENT_DAEMON_H
#define INPUT_EVENT_DAEMON_H

#define test_bit(array, bit) ((array)[(bit)/8] & (1 << ((bit)%8)))

#define MAX_MODIFIERS 	 4
#define MAX_EVENTS		 64
#define MAX_LISTENER	 32

#define PROGNAME  "input-event-daemon"

const char *config_file = "sample.conf";
unsigned long min_timeout = 3600;

struct {
	unsigned short verbose;
	unsigned short monitor;
} settings;

/*
 * Event structs 
 */

typedef struct key_event {
    const char *code;
    const char *modifiers[MAX_MODIFIERS];
    size_t     modifier_n;
    const char *exec;
} key_event_t;


typedef struct idle_event {
    unsigned long timeout;
    const char *exec;
} idle_event_t;

typedef struct switch_event {
    const char *code;
    int value;
    const char *exec;
} switch_event_t;

const char *listen[MAX_LISTENER];

key_event_t       key_events[MAX_EVENTS];
idle_event_t     idle_events[MAX_EVENTS];
switch_event_t switch_events[MAX_EVENTS];

size_t    key_event_n = 0;
size_t   idle_event_n = 0;
size_t switch_event_n = 0;

#endif /* INPUT_EVENT_DAEMON_H */
