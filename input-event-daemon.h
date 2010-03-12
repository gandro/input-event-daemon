#include "input-event-table.h"

#define test_bit(bit, array) ((array)[(bit)/8] & (1 << ((bit)%8)))

#define MAX_KEYSTACK 	 5
#define MAX_EVENTS		 64
#define MAX_LISTENER	 32

char *config_file = "sample.conf";

char *listen[MAX_LISTENER];
int listen_n = 0;

int min_timeout = 60;

/*
 * Storage structures
 */

struct key_stack {
	const char *stack[MAX_KEYSTACK];
	int length;
};

struct event_list {
	void *list[MAX_EVENTS];
	int length;
} key_events, switch_events, idle_events;

/*
 * Event structs 
 */

struct key_event {
    struct key_stack stack;
    const char *exec;
} current_keyevent;

struct switch_event {
    const char *code;
    int value;
    const char *exec;
};

struct idle_event {
    int idle_secs;
    const char *exec;
};
