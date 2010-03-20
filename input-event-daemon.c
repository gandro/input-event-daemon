#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ctype.h>
#include <getopt.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <sys/wait.h>
#include <sys/select.h>

#include <linux/input.h>

#include "input-event-daemon.h"
#include "input-event-table.h"


static int key_event_compare(const key_event_t *a, const key_event_t *b) {
    int i, r_cmp;

    if((r_cmp = strcmp(a->code, b->code)) != 0) {
        return r_cmp;
    } else if(a->modifier_n != b->modifier_n) {
        return (a->modifier_n - b->modifier_n);
    } else {
        for(i=0; i < a->modifier_n; i++) {
            if((r_cmp = strcmp(a->modifiers[i], b->modifiers[i])) != 0) {
                return r_cmp;
            }
        }
    }
    return 0;
}

static const char *key_event_name(unsigned int code) {
    if(code < KEY_MAX && KEY_NAME[code] != NULL) {
        return KEY_NAME[code];
    } else {
        return "UNKNOWN";
    }
}

static const char *key_event_modifier_name(const char* code) {
    if(
        strcmp(code, "LEFTCTRL") == 0 || strcmp(code, "RIGHTCTRL") == 0
    ) {
        return "CTRL";
    } else if(
        strcmp(code, "LEFTALT") == 0 || strcmp(code, "RIGHTALT") == 0
    ) {
        return "ALT";
    } else if(
        strcmp(code, "LEFTSHIFT") == 0 || strcmp(code, "RIGHTSHIFT") == 0
    ) {
        return "SHIFT";
    } else if(
        strcmp(code, "LEFTMETA") == 0 || strcmp(code, "RIGHTMETA") == 0
    ) {
        return "META";
    }

    return code;
}

static key_event_t
*key_event_parse(unsigned int code, int pressed, const char *src) {
    key_event_t *fired_key_event = NULL;
    static key_event_t current_key_event = {
        .code = NULL,
        .modifier_n = 0
    };

    if(pressed) {

        /* ignore if repeated */
        if(
            current_key_event.code != NULL &&
            strcmp(current_key_event.code, key_event_name(code)) == 0
        ) {
            return NULL;
        }

        /* if previous key present and modifier limit not yet reached */
        if(
            current_key_event.code != NULL &&
            current_key_event.modifier_n < MAX_MODIFIERS
        ) {
            int i;

            /* if not already in the modifiers array */
            for(i=0; i < current_key_event.modifier_n; i++) {
                if(strcmp(
                        current_key_event.modifiers[i], current_key_event.code
                    ) == 0
                ) {
                    return NULL;
                }
            }

            /* add previous key as modifier */
            current_key_event.modifiers[current_key_event.modifier_n++] =
                key_event_modifier_name(current_key_event.code);

        }

        current_key_event.code = key_event_name(code);

        if(current_key_event.modifier_n == 0) {
            if(conf.monitor) {
                printf("%s:\n  keys      : ", src);
                printf("%s\n\n", current_key_event.code);
            }

            fired_key_event = bsearch(
                &current_key_event,
                key_events,
                key_event_n,
                sizeof(key_event_t),
                (int (*)(const void *, const void *)) key_event_compare
            );
        }


    } else {
        int i, new_modifier_n = 0;
        const char *new_modifiers[MAX_MODIFIERS], *modifier_code;

        if(current_key_event.code != NULL && current_key_event.modifier_n > 0) {

            if(conf.monitor) {
                printf("%s:\n  keys     : ", src);
                for(i=0; i < current_key_event.modifier_n; i++) {
                     printf("%s + ", current_key_event.modifiers[i]);
                }
                printf("%s\n\n", current_key_event.code);
            }

            qsort(
                current_key_event.modifiers,
                current_key_event.modifier_n,
                sizeof(const char*),
                (int (*)(const void *, const void *)) strcmp
            );

            fired_key_event = bsearch(
                &current_key_event,
                key_events,
                key_event_n,
                sizeof(key_event_t),
                (int (*)(const void *, const void *)) key_event_compare
            );

        }

        if(
            current_key_event.code != NULL &&
            strcmp(current_key_event.code, key_event_name(code)) == 0
        ) {
            current_key_event.code = NULL;
        }

        /* remove released key from modifiers */
        modifier_code = key_event_modifier_name(key_event_name(code));
        for(i=0; i < current_key_event.modifier_n; i++) {
            if(strcmp(current_key_event.modifiers[i], modifier_code) != 0) {
                new_modifiers[new_modifier_n++] =
                    current_key_event.modifiers[i];
            }
        }
        memcpy(current_key_event.modifiers, new_modifiers,
            MAX_MODIFIERS*sizeof(const char*));
        current_key_event.modifier_n = new_modifier_n;

    }

    if(conf.verbose && fired_key_event) {
        int i;

        fprintf(stderr, "\nkey_event:\n"
                        "  code     : ");
        for(i=0; i < fired_key_event->modifier_n; i++) {
            fprintf(stderr, "%s + ", fired_key_event->modifiers[i]);
        }

        fprintf(stderr, "%s\n"
                        "  source   : %s\n"
                        "  exec     : \"%s\"\n\n",
                        fired_key_event->code,
                        src,
                        fired_key_event->exec
        );
    }

    return fired_key_event;
}

static int idle_event_compare(const idle_event_t *a, const idle_event_t *b) {
    return (a->timeout - b->timeout);
}

static int idle_event_parse(unsigned long idle) {
    idle_event_t *fired_idle_event;
    idle_event_t current_idle_event = {
        .timeout = idle
    };

    fired_idle_event = bsearch(
        &current_idle_event,
        idle_events,
        idle_event_n,
        sizeof(idle_event_t),
        (int (*)(const void *, const void *)) idle_event_compare
    );

    if(fired_idle_event != NULL) {
        if(conf.verbose) {
            fprintf(stderr, "\nidle_event:\n");

            if(fired_idle_event->timeout == IDLE_RESET) {
                fprintf(stderr, "  time     : idle reset\n");
            } else {
                fprintf(stderr, "  time     : %ldh %ldm %lds\n",
                                fired_idle_event->timeout / 3600,
                                fired_idle_event->timeout % 3600 / 60,
                                fired_idle_event->timeout % 60
                );
            }

            fprintf(stderr, "  exec     : \"%s\"\n\n", fired_idle_event->exec);
        }
        daemon_exec(fired_idle_event->exec);
    }

    return (fired_idle_event != NULL);
}

static int
switch_event_compare(const switch_event_t *a, const switch_event_t *b) {
    int r_cmp;

    if((r_cmp = strcmp(a->code, b->code)) != 0) {
        return r_cmp;
    } else {
        return (a->value - b->value);
    }
}

static const char *switch_event_name(unsigned int code) {
    if(code < SW_MAX && SW_NAME[code] != NULL) {
        return SW_NAME[code];
    } else {
        return "UNKNOWN";
    }
}

static switch_event_t
*switch_event_parse(unsigned int code, int value, const char *src) {
    switch_event_t *fired_switch_event;
    switch_event_t current_switch_event = {
        .code = switch_event_name(code),
        .value = value
    };

    if(conf.monitor) {
        printf("%s:\n  switch   : %s:%d\n\n",
            src,
            current_switch_event.code,
            current_switch_event.value
        );
    }

    fired_switch_event = bsearch(
        &current_switch_event,
        switch_events,
        switch_event_n,
        sizeof(switch_event_t),
        (int (*)(const void *, const void *)) switch_event_compare
    );

    if(conf.verbose && fired_switch_event) {
        fprintf(stderr, "\nswitch_event:\n"
                        "  switch   : %s:%d\n"
                        "  source   : %s\n"
                        "  exec     : \"%s\"\n\n",
                        fired_switch_event->code,
                        fired_switch_event->value,
                        src,
                        fired_switch_event->exec
        );
    }

    return fired_switch_event;
}

void input_open_all_listener() {
    int i, listen_len = 0;
    char filename[32];

    for(i=0; i<MAX_LISTENER; i++) {
        snprintf(filename, sizeof(filename), "/dev/input/event%d", i);
        if(access(filename, R_OK) != 0) {
            if(errno != ENOENT) {
                fprintf(stderr, PROGRAM": access(%s): %s\n",
                    filename, strerror(errno));
            }
            continue;
        }
        conf.listen[listen_len++] = strdup(filename);
    }
}

void input_list_devices() {
    int fd, i, e;
    unsigned char evmask[EV_MAX/8 + 1];

    for(i=0; i < MAX_LISTENER && conf.listen[i] != NULL; i++) {
        char phys[64] = "no physical path", name[256] = "Unknown Device";

        fd = open(conf.listen[i], O_RDONLY);
        if(fd < 0) {
            fprintf(stderr, PROGRAM": open(%s): %s\n",
                conf.listen[i], strerror(errno));
            continue;
        }

        memset(evmask, '\0', sizeof(evmask));

        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys);
        ioctl(fd, EVIOCGBIT(0, sizeof(evmask)), evmask);

        printf("%s:\n", conf.listen[i]);
        printf("  name     : %s\n", name);
        printf("  phys     : %s\n", phys);

        printf("  features :");
        for(e=0; e<EV_MAX; e++) {
            if (test_bit(evmask, e)) {
                const char *feature = "unknown";
                switch(e) {
                    case EV_SYN: feature = "syn";      break;
                    case EV_KEY: feature = "keys";     break;
                    case EV_REL: feature = "relative"; break;
                    case EV_ABS: feature = "absolute"; break;
                    case EV_MSC: feature = "reserved"; break;
                    case EV_LED: feature = "leds";     break;
                    case EV_SND: feature = "sound";    break;
                    case EV_REP: feature = "repeat";   break;
                    case EV_FF:  feature = "feedback"; break;
                    case EV_SW:  feature = "switch";   break;
                }
                printf(" %s", feature);
            }
        }
        printf("\n\n");
        close(fd);
    }
}

static void input_parse_event(struct input_event *event, const char *src) {
    key_event_t *fired_key_event;
    switch_event_t *fired_switch_event;

    switch(event->type) {
        case EV_KEY:
            fired_key_event = key_event_parse(event->code, event->value, src);

            if(fired_key_event != NULL) {
                daemon_exec(fired_key_event->exec);
            }
            break;
        case EV_SW:
            fired_switch_event =
                switch_event_parse(event->code, event->value, src);

            if(fired_switch_event != NULL) {
                daemon_exec(fired_switch_event->exec);
            }

            break;
    }
}


void config_parse_file() {
    FILE *config_fd;
    char buffer[512], *line;
    char *section = NULL;
    char *key, *value, *ptr;
    const char *error = NULL;
    int line_num = 0;
    int listen_len = 0;

    if((config_fd = fopen(conf.configfile, "r")) == NULL) {
        fprintf(stderr, PROGRAM": fopen(%s): %s\n",
            conf.configfile, strerror(errno));
        exit(EXIT_FAILURE);
    }

    memset(buffer, 0, sizeof(buffer));

    while(fgets(buffer, sizeof(buffer), config_fd) != NULL) {
        line = config_trim_string(buffer);
        line_num++;

        if(line[0] == '\0' || line[0] == '#') {
            continue;
        }

        if((ptr = strchr(line, '#'))) {
            *ptr = '\0';
        }

        if(line[0] == '[' && line[strlen(line)-1] == ']') {
            if(section != NULL) {
                free(section);
            }
            line[strlen(line)-1] = '\0';
            section = config_trim_string(strdup(line+1));
            continue;
        }

        key = value = line;
        strsep(&value, "=");
        if(value == NULL) {
            error = "Invalid syntax!";
            goto print_error;
        }

        key = config_trim_string(key);
        value = config_trim_string(value);

        if(section == NULL) {
            error = "Missing section!";
        } else if(strlen(key) == 0 || strlen(value) == 0) {
            error = "Invlaid syntax!";
        } else if(strcasecmp(section, "Global") == 0) {
            if(strcmp(key, "listen") == 0) {
                if(listen_len < MAX_LISTENER) {
                    conf.listen[listen_len++] = strdup(value);
                } else {
                    error = "Listener limit exceeded!";
                }
            } else {
                error = "Unkown option!";
            }
        } else if(strcasecmp(section, "Keys") == 0) {
            error = config_key_event(key, value);
        } else if(strcasecmp(section, "Idle") == 0) {
            error = config_idle_event(key, value);
        } else if(strcasecmp(section, "Switches") == 0) {
            error = config_switch_event(key, value);
        } else {
            error = "Unknown section!";
            section = NULL;
        }

        print_error:
        if(error != NULL) {
            fprintf(stderr, PROGRAM": %s (%s:%d)\n",
                error, conf.configfile, line_num);
        }


    }

    qsort(key_events, key_event_n, sizeof(key_event_t),
        (int (*)(const void *, const void *)) key_event_compare);

    qsort(idle_events, idle_event_n, sizeof(idle_event_t),
        (int (*)(const void *, const void *)) idle_event_compare);

    qsort(switch_events, switch_event_n, sizeof(switch_event_t),
        (int (*)(const void *, const void *)) switch_event_compare);

    if(section != NULL) {
        free(section);
    }

    fclose(config_fd);
}

static const char *config_key_event(char *shortcut, char *exec) {
    int i;
    char *code, *modifier;
    key_event_t *new_key_event;

    if(key_event_n >= MAX_EVENTS) {
        return "Key event limit exceeded!";
    } else {
        new_key_event = &key_events[key_event_n++];
    }

    new_key_event->modifier_n = 0;
    for(i=0; i < MAX_MODIFIERS; i++) {
        new_key_event->modifiers[i] = NULL;
    }

    if((code = strrchr(shortcut, '+')) != NULL) {
        *code = '\0';
        code = config_trim_string(code+1);

        new_key_event->code = strdup(code);

        modifier = strtok(shortcut, "+");
        while(modifier != NULL && new_key_event->modifier_n < MAX_MODIFIERS) {
            modifier = config_trim_string(modifier);
            new_key_event->modifiers[new_key_event->modifier_n++] =
                strdup(modifier);

            modifier = strtok(NULL, "+");
        }
    } else {
        new_key_event->code = strdup(shortcut);
    }

    new_key_event->exec = strdup(exec);

    qsort(new_key_event->modifiers, new_key_event->modifier_n,
        sizeof(const char*), (int (*)(const void *, const void *)) strcmp);

    return NULL;
}

static const char *config_idle_event(char *timeout, char *exec) {
    idle_event_t *new_idle_event;
    unsigned long count;
    char *unit;

    if(idle_event_n >= MAX_EVENTS) {
        return "Idle event limit exceeded!";
    } else {
        new_idle_event = &idle_events[idle_event_n++];
    }

    new_idle_event->timeout = 0;
    new_idle_event->exec = strdup(exec);

    if(strcasecmp(timeout, "RESET") == 0) {
        new_idle_event->timeout = IDLE_RESET;
        return NULL;
    }

    while(*timeout) {
        while(*timeout && !isdigit(*timeout)) timeout++;
        count = strtoul(timeout, &unit, 10);

        switch(*unit) {
        case 'h':
            new_idle_event->timeout += count * 3600;
            break;
        case 'm':
            new_idle_event->timeout += count * 60;
            break;
        case 's':
        default:
            new_idle_event->timeout += count;
            break;
        }

        timeout = unit;
    }

    conf.min_timeout = config_min_timeout(
        new_idle_event->timeout, conf.min_timeout);

    return NULL;
}

static const char *config_switch_event(char *switchcode, char *exec) {
    char *code, *value;
    switch_event_t *new_switch_event;

    if(switch_event_n >= MAX_EVENTS) {
        return "Switch event limit exceeded!";
    } else {
        new_switch_event = &switch_events[switch_event_n++];
    }

    code = value = switchcode;
    strsep(&value, ":");
    if(value == NULL) {
        switch_event_n--;
        return "Invalid switch identifier";
    }

    code = config_trim_string(code);
    value = config_trim_string(value);

    new_switch_event->code = strdup(code);
    new_switch_event->value = atoi(value);
    new_switch_event->exec = strdup(exec);

    return NULL;
}

static unsigned int config_min_timeout(unsigned long a, unsigned long b) {
    if(b == 0) return a;

    return config_min_timeout(b, a % b);
}

static char *config_trim_string(char *str) {
    char *end;

    while(isspace(*str)) str++;

    end = str + strlen(str) - 1;
    while(end > str && isspace(*end)) end--;

    *(end+1) = '\0';

    return str;
}

void daemon_init() {
    int i;

    conf.configfile  = "/etc/input-event-daemon.conf";

    conf.monitor     = 0;
    conf.verbose     = 0;
    conf.daemon      = 1;

    conf.min_timeout = 3600;

    for(i=0; i<MAX_LISTENER; i++) {
        conf.listen[i]    = NULL;
        conf.listen_fd[i] = 0;
    }
}

void daemon_start_listener() {
    int i, select_r, fd_len;
    unsigned long tms_start, tms_end, idle_time = 0;
    fd_set fdset, initial_fdset;
    struct input_event event;
    struct timeval tv, tv_start, tv_end;

    /* ignored forked processes */
    signal(SIGCHLD, SIG_IGN);

    FD_ZERO(&initial_fdset);
    for(i=0; i < MAX_LISTENER && conf.listen[i] != NULL; i++) {
        conf.listen_fd[i] = open(conf.listen[i], O_RDONLY);

        if(conf.listen_fd[i] < 0) {
            fprintf(stderr, PROGRAM": open(%s): %s\n",
                conf.listen[i], strerror(errno));
            exit(EXIT_FAILURE);
        }
        FD_SET(conf.listen_fd[i], &initial_fdset);
    }

    fd_len = i;

    if(fd_len == 0) {
        fprintf(stderr, PROGRAM": no listener found!\n");
        return;
    }

    if(conf.verbose) {
        fprintf(stderr, PROGRAM": Start listening on %d devices...\n", fd_len);
    }

    if(conf.monitor) {
        printf(PROGRAM": Monitoring mode started. Press CTRL+C to abort.\n\n");
    } else if(conf.daemon) {
        if(daemon(1, conf.verbose) < 0) {
            perror(PROGRAM": daemon()");
            exit(EXIT_FAILURE);
        }
    }

    while(1) {
        fdset = initial_fdset;
        tv.tv_sec = conf.min_timeout;
        tv.tv_usec = 0;

        gettimeofday(&tv_start, NULL);

        select_r = select(conf.listen_fd[fd_len-1]+1, &fdset, NULL, NULL, &tv);

        gettimeofday(&tv_end, NULL);

        if(select_r < 0) {
            perror(PROGRAM": select()");
            break;
        } else if(select_r == 0) {
            idle_time += conf.min_timeout;
            idle_event_parse(idle_time);
            continue;
        }

        tms_start = tv_start.tv_sec * 1000 + tv_start.tv_usec / 1000;
        tms_end   = tv_end.tv_sec  *  1000 + tv_end.tv_usec  /  1000;

        if(tms_end - tms_start > 750) {
            idle_event_parse(IDLE_RESET);
            idle_time = 0;
        }

        for(i=0; i<fd_len; i++) {
            if(FD_ISSET(conf.listen_fd[i], &fdset)) {
                if(read(conf.listen_fd[i], &event, sizeof(event)) < 0) {
                    fprintf(stderr, PROGRAM": read(%s): %s\n",
                        conf.listen[i], strerror(errno));
                    break;
                }
                input_parse_event(&event, conf.listen[i]);
            }
        }
    }
}

static void daemon_exec(const char *command) {
    pid_t pid = fork();
    if(pid == 0) {
        const char *args[] = {
            "sh", "-c", command, NULL
        };


        if(!conf.verbose) {
            int null_fd = open("/dev/null", O_RDWR, 0);
            if(null_fd > 0) {
                dup2(null_fd, STDIN_FILENO);
                dup2(null_fd, STDOUT_FILENO);
                dup2(null_fd, STDERR_FILENO);
                if(null_fd > STDERR_FILENO) {
                    close(null_fd);
                }
            }
        }

        signal(SIGINT,  SIG_IGN);
        signal(SIGQUIT, SIG_IGN);

        execv("/bin/sh", (char *const *) args);
        _exit(127);
    } else if(pid < 0) {
        perror(PROGRAM": fork()");
    }

    return;
}

void daemon_clean() {
    int i, j;

    if(conf.verbose) {
        fprintf(stderr, "\n"PROGRAM": Exiting...\n");
    }

    for(i=0; i<key_event_n; i++) {
        free((void*) key_events[i].code);
        for(j=0; j<key_events[i].modifier_n; j++) {
            free((void*) key_events[i].modifiers[j]);
        }
        free((void*) key_events[i].exec);
    }
    key_event_n = 0;

    for(i=0; i<idle_event_n; i++) {
        free((void*) idle_events[i].exec);
    }
    idle_event_n = 0;

    for(i=0; i<switch_event_n; i++) {
        free((void*) switch_events[i].code);
        free((void*) switch_events[i].exec);
    }
    switch_event_n = 0;

    for(i=0; i < MAX_LISTENER && conf.listen[i] != NULL; i++) {
        free((void*) conf.listen[i]);
        conf.listen[i] = NULL;
        if(conf.listen_fd[i]) {
            close(conf.listen_fd[i]);
        }
    }

    _exit(EXIT_SUCCESS);
}

static void daemon_print_help() {
    printf("Usage:\n\n"
            "    "PROGRAM" "
            "[ [ --monitor | --list | --help | --version ] |\n"
            "                         "
            "[--config=FILE] [--verbose] [--no-daemon] ]\n"
            "\n"
            "Available Options:\n"
            "\n"
            "    -m, --monitor       Start in monitoring mode\n"
            "    -l, --list          List all input devices and quit\n"
            "    -c, --config FILE   Use specified config file\n"
            "    -v, --verbose       Verbose output\n"
            "    -D, --no-daemon     Don't run in background\n"
            "\n"
            "    -h, --help          Show this help and quit\n"
            "    -V, --version       Show version number and quit\n"
            "\n"
    );
    exit(EXIT_SUCCESS);
}

static void daemon_print_version() {
    printf(PROGRAM" "VERSION"\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    int result, arguments = 0;
    static const struct option long_options[] = {
        { "monitor",   no_argument,       0, 'm' },
        { "list",      no_argument,       0, 'l' },
        { "config",    required_argument, 0, 'c' },
        { "verbose",   no_argument,       0, 'v' },
        { "no-daemon", no_argument,       0, 'D' },
        { "help",      no_argument,       0, 'h' },
        { "version",   no_argument,       0, 'V' },
        {NULL,         0,              NULL,  0  }
    };

    daemon_init();

    atexit(daemon_clean);
    signal(SIGTERM, daemon_clean);
    signal(SIGINT,  daemon_clean);

    while (optind < argc) {
        result = getopt_long(argc, argv, "mlc:vDhV", long_options, NULL);
        arguments++;

        switch(result) {
            case 'm': /* monitor */
                if(arguments > 1 || optind < argc) {
                    fprintf(stderr, PROGRAM": option --monitor "
                        "can not be combined with other options!\n");
                    return EXIT_FAILURE;
                }
                conf.monitor = 1;
                break;
            case 'l': /* list */
                if(arguments > 1 || optind < argc) {
                    fprintf(stderr, PROGRAM": option --list "
                        "can not be combined with other options!\n");
                    return EXIT_FAILURE;
                }
                input_open_all_listener();
                input_list_devices();
                return EXIT_SUCCESS;
                break;
            case 'c': /* config */
                conf.configfile = optarg;
                break;
            case 'v': /* verbose */
                conf.verbose = 1;
                break;
            case 'D': /* no-daemon */
                conf.daemon = 0;
                break;
            case 'h': /* help */
                daemon_print_help();
                break;
            case 'V': /* version */
                daemon_print_version();
                break;
            default: /* unknown */
                break;
        }
    }

    if(conf.monitor) {
        input_open_all_listener();
    } else {
        config_parse_file();
    }

    daemon_start_listener();

    return EXIT_SUCCESS;
}
