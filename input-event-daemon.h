#ifndef INPUT_EVENT_DAEMON_H
#define INPUT_EVENT_DAEMON_H

#define PROGRAM  "input-event-daemon"
#define VERSION  "0.1.2"

#define MAX_MODIFIERS      4
#define MAX_LISTENER       32
#define MAX_EVENTS         64

#define test_bit(array, bit) ((array)[(bit)/8] & (1 << ((bit)%8)))

/**
 * Global Configuration 
 *
 */

struct {
    const char      *configfile;

    unsigned char   monitor;
    unsigned char   verbose;
    unsigned char   daemon;

    unsigned long   min_timeout;

    const char      *listen[MAX_LISTENER];
    int             listen_fd[MAX_LISTENER];
} conf;

/**
 * Event Structs 
 *
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

/**
 * Event Lists 
 *
 */

key_event_t       key_events[MAX_EVENTS];
idle_event_t     idle_events[MAX_EVENTS];
switch_event_t switch_events[MAX_EVENTS];

size_t    key_event_n = 0;
size_t   idle_event_n = 0;
size_t switch_event_n = 0;

/**
 * Functions 
 *
 */

static int
    key_event_compare(const key_event_t *a, const key_event_t *b);
static const char
    *key_event_name(unsigned int code);
static const char
    *key_event_modifier_name(const char* code);
static key_event_t 
    *key_event_parse(unsigned int code, int pressed, const char *src);


static int idle_event_compare(const idle_event_t *a, const idle_event_t *b);
static int idle_event_parse(unsigned long idle);


static int
    switch_event_compare(const switch_event_t *a, const switch_event_t *b);
static const char
    *switch_event_name(unsigned int code);
static switch_event_t
    *switch_event_parse(unsigned int code, int value, const char *src);


void        input_open_all_listener();
void        input_list_devices();
static void input_parse_event(struct input_event *event, const char *src);


void                config_parse_file();
static const char   *config_key_event(char *shortcut, char *exec);
static const char   *config_idle_event(char *timeout, char *exec);
static const char   *config_switch_event(char *switchcode, char *exec);
static unsigned int config_min_timeout(unsigned long a, unsigned long b);
static char         *config_trim_string(char *str);

void        daemon_init();
void        daemon_start_listener();
static void daemon_exec(const char *command);
void        daemon_clean();
static void daemon_print_help();
static void daemon_print_version();

#endif /* INPUT_EVENT_DAEMON_H */
