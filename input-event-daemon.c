#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/select.h>
#include <linux/input.h>

#include "input-event-daemon.h"
#include "input-event-table.h"

static void input_list_devices() {
    int fd, i, e;
    unsigned char evmask[EV_MAX/8 + 1];

    for(i=0; i < MAX_LISTENER && listen[i] != NULL; i++) {
        char phys[64] = "no physical path", name[256] = "Unknown Device";

        fd = open(listen[i], O_RDONLY);
        if(fd < 0) {
            if(errno != ENOENT) {
                fprintf(stderr, PROGNAME": open(%s): %s\n", 
                    listen[i], strerror(errno));
            }
            continue;
        }

		memset(evmask, '\0', sizeof(evmask));

        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys);
        ioctl(fd, EVIOCGBIT(0, sizeof(evmask)), evmask);

        printf("%s\n", listen[i]);
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
    if(code < KEY_MAX) {
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

        /* add previous key to modifiers (if not already there) */
        if(
            current_key_event.code != NULL &&
            current_key_event.modifier_n < MAX_MODIFIERS
        ) {
            int i;

            for(i=0; i < current_key_event.modifier_n; i++) {
                if(strcmp(
                        current_key_event.modifiers[i], current_key_event.code
                    ) == 0
                ) {
                    return NULL;
                }
            }

            if(strcmp(current_key_event.code, key_event_name(code)) != 0) {
                current_key_event.modifiers[current_key_event.modifier_n++] = 
                    key_event_modifier_name(current_key_event.code);
            }
        }

        current_key_event.code = key_event_name(code);

    } else {
        int i, new_modifier_n = 0;
        const char *new_modifiers[MAX_MODIFIERS], *modifier_code;

        if(current_key_event.code != NULL) {

			if(settings.monitor) {
				printf("%s:\n    [Keys] ", src);
				for(i=0; i < current_key_event.modifier_n; i++) {
             		printf("%s+", current_key_event.modifiers[i]);
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

            /*if(fired_key_event != NULL) {
    			fprintf(stderr, "Executing: %s\n", fired_key_event->exec);
            }*/

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

    return fired_key_event;
}

static int idle_event_compare(const idle_event_t *a, const idle_event_t *b) {
	return (a->timeout - b->timeout);
}

static void idle_event_parse(unsigned long idle) {
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
		fprintf(stderr, "Executing: %s\n", fired_idle_event->exec);
    }
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

static switch_event_t
*switch_event_parse(unsigned int code, int value, const char *src) {
	switch_event_t *fired_switch_event;
	switch_event_t current_switch_event = {
		.code = SW_NAME[code],
		.value = value
	};

	if(settings.monitor) {
		printf("%s:\n    [Switches] %s:%d\n\n",
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

	return fired_switch_event;
}

static void input_parse_event(struct input_event *event, const char *src) {
    key_event_t *fired_key_event;
	switch_event_t *fired_switch_event;

    switch(event->type) {
        case EV_KEY:
            fired_key_event = key_event_parse(event->code, event->value, src);

            if(fired_key_event != NULL) {
    			fprintf(stderr, "Executing: %s\n", fired_key_event->exec);
            }
            break;
		case EV_SW:
			fired_switch_event = 
				switch_event_parse(event->code, event->value, src);

            if(fired_switch_event != NULL) {
    			fprintf(stderr, "Executing: %s\n", fired_switch_event->exec);
            }
			break;
    }
}

static void input_all_listener() {
	int i, listen_len = 0;
    char filename[32];

    for(i=0; i<MAX_LISTENER; i++) {
        snprintf(filename, sizeof(filename), "/dev/input/event%d", i);
		if(access(filename, R_OK) != 0) {
            if(errno != ENOENT) {
                fprintf(stderr, PROGNAME": access(%s): %s\n", 
					filename, strerror(errno));
            }
			continue;
		}
		listen[listen_len++] = strdup(filename);
	}
}

static void input_start_listener() {
    int i, select_r, fd_len, fd[MAX_LISTENER];
	unsigned long idle_time = 0;
    fd_set fdset, initial_fdset;
    struct input_event event;
    struct timeval tv;

    FD_ZERO(&initial_fdset);
    for(i=0; i < MAX_LISTENER && listen[i] != NULL; i++) {
        fd[i] = open(listen[i], O_RDONLY);

        if(fd[i] < 0) {
            fprintf(stderr, PROGNAME": open(%s): %s\n", 
                listen[i], strerror(errno));
            exit(EXIT_FAILURE);
        }
        FD_SET(fd[i], &initial_fdset);
    }

    fd_len = i;

	if(fd_len == 0) {
    	fprintf(stderr, PROGNAME": no listener found!\n");
		return;
	}

    while(1) {
        fdset = initial_fdset;
		tv.tv_sec = min_timeout;
		tv.tv_usec = 0;

        select_r = select(fd[fd_len-1]+1, &fdset, NULL, NULL, &tv);

        if(select_r < 0) {
            perror(PROGNAME": select()");
            break;
        } else if(select_r == 0) {
			idle_time += min_timeout;
            printf("Timeout! idle = %ld\n", idle_time);
			idle_event_parse(idle_time);
			continue;
		} else if(idle_time > 0) {
			idle_time = 0;
			idle_event_parse(0);
		}


        for(i=0; i<fd_len; i++) {
            if(FD_ISSET(fd[i], &fdset)) {
                if(read(fd[i], &event, sizeof(event)) < 0) {
                    fprintf(stderr, PROGNAME": read(%s): %s\n",
                        listen[i], strerror(errno));
                    break;
                }
                input_parse_event(&event, listen[i]);
            }
        }
    }
}

static char *config_trim_string(char *str) {
	char *end;

	while(isspace(*str)) str++;

	end = str + strlen(str) - 1;
	while(end > str && isspace(*end)) end--;

	*(end+1) = '\0';

	return str;
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

static unsigned int config_min_timeout(unsigned long a, unsigned long b) {
    if(b == 0) return a;

    return config_min_timeout(b, a % b);
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

    min_timeout = config_min_timeout(new_idle_event->timeout, min_timeout);

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


static void config_parse_file() {
	FILE *config_fd;
	char buffer[512], *line;
	char *section = NULL;
	char *key, *value, *ptr;
    const char *error = NULL;
	int line_num = 0;
    int listen_len = 0;

	if((config_fd = fopen(config_file, "r")) == NULL) {
        fprintf(stderr, PROGNAME": fopen(%s): %s\n",
            config_file, strerror(errno));
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
        } else if(strcmp(section, "Global") == 0) {
			if(strcmp(key, "listen") == 0) {
				if(listen_len < MAX_LISTENER) {
					listen[listen_len++] = strdup(value);
				} else {
					error = "Listener limit exceeded!";
				}
			} else {
				error = "Unkown option!";
			}
		} else if(strcmp(section, "Keys") == 0) {
            error = config_key_event(key, value);
		} else if(strcmp(section, "Idle") == 0) {
            error = config_idle_event(key, value);
		} else if(strcmp(section, "Switches") == 0) {
            error = config_switch_event(key, value);
		} else {
			error = "Unknown section!";
			section = NULL;
		}

		print_error:
        if(error != NULL) {
    		fprintf(stderr, PROGNAME": %s (%s:%d)\n", 
				error, config_file, line_num);
        }

		
	}

	qsort(key_events, key_event_n, sizeof(key_event_t),
		(int (*)(const void *, const void *)) key_event_compare);


	qsort(idle_events, idle_event_n, sizeof(idle_event_t),
		(int (*)(const void *, const void *)) idle_event_compare);

	qsort(switch_events, switch_event_n, sizeof(switch_event_t),
		(int (*)(const void *, const void *)) switch_event_compare);

/*    int i=0;
key_event_t *new_key_event;
    for(i=0; i< key_event_n; i++) {
        new_key_event = &key_events[i];

        printf("modifier_n: %d\n", new_key_event->modifier_n);
int j;
        for(j=0; j < new_key_event->modifier_n; j++) {
             printf("%s+", new_key_event->modifiers[j]);
        }

        printf("%s\n", new_key_event->code);
    }
*/
	if(section != NULL) {
		free(section);
	}
	fclose(config_fd);
}

static void config_clean_events() {
	int i, j;

	for(i=0; i<key_event_n; i++) {
		free((void*) key_events[i].code);
		for(j=0; j<key_events[i].modifier_n; j++) {
			free((void*) key_events[i].modifiers[j]);
		}
		free((void*) key_events[i].exec);
	}

	for(i=0; i<idle_event_n; i++) {
		free((void*) idle_events[i].exec);
	}

	for(i=0; i<switch_event_n; i++) {
		free((void *) switch_events[i].code);
		free((void*) switch_events[i].exec);
	}

    for(i=0; i < MAX_LISTENER && listen[i] != NULL; i++) {
		free((void*) listen[i]);
	}
}

int main(int argc, char *argv[]) {
	settings.monitor = 0;
	if(settings.monitor) {
		input_all_listener();
	} else {
		config_parse_file();
	}
    input_start_listener();
	config_clean_events();

    return 0;
}
