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

static void list_input_devices() {
    int fd, i, e;
    char filename[32];
    unsigned char evmask[EV_MAX/8 + 1];

    for(i=0; i<32; i++) {
        char phys[32] = "no physical path", name[256] = "Unknown Device";

        snprintf(filename, sizeof(filename), "/dev/input/event%d", i);
        fd = open(filename, O_RDONLY);
        if(fd < 0) {
            if(errno != ENOENT) {
                fprintf(stderr, "input-event-daemon: open(%s): %s\n", 
                    filename, strerror(errno));
            }
            continue;
        }

        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys);
        ioctl(fd, EVIOCGBIT(0, sizeof(evmask)), evmask);

        printf("%s\n", filename);
        printf("  name     : %s\n", name);
        printf("  phys     : %s\n", phys);

        printf("  features :");
        for(e=0; e<EV_MAX; e++) {
            if (test_bit(e, evmask)) {
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

static void keystack_add(const char *key_name, struct key_stack *st) {
	int i;

	if(st->length < MAX_KEYSTACK) {
		for(i=0; i<st->length; i++) {
			if(strcmp(st->stack[i], key_name) == 0) {
				return;
			}
		}
		st->stack[st->length++] = key_name;
	}
}

static void keystack_clean(struct key_stack *st) {
	int i;
	for(i=0; i<MAX_KEYSTACK; i++) {
		st->stack[i] = NULL;
	}
	st->length = 0;
}

static void keystack_sort(struct key_stack *st) {
	qsort(st->stack, st->length,
		sizeof(char*), (int (*)(const void *, const void *)) strcmp);
}

static int 
keystack_compare(const struct key_stack *a, const struct key_stack *b) {

	int i, rc;

	if((rc = (a->length - b->length)) != 0) {
		return rc;
	}

	for(i=0; i<a->length; i++) {
		if((rc = strcmp(a->stack[i], b->stack[i])) != 0) {
			return rc;
		}
	}

	return 0;
}

static int
key_event_compare(const struct key_event *a, const struct key_event *b) {
	return keystack_compare(&a->stack, &b->stack);
}

static int
switch_event_compare(const struct switch_event *a, const struct switch_event *b) {
	return (a->code - b->code);
}

static void parse_input_event(struct input_event *event) {
    switch(event->type) {
        case EV_KEY:
			if(event->value == 0) {
				int i;
				keystack_sort(&current_keyevent.stack);
				for(i=0; i<current_keyevent.stack.length; i++) {
					printf("%s+", current_keyevent.stack.stack[i]);
				}
				printf("\n");
				struct key_event *fire = bsearch(&current_keyevent, 
					key_events.list, key_events.length,
					sizeof(struct key_event),
					(int(*)(const void*,const void*)) key_event_compare
				);
				if(fire != NULL) {
					printf("Executing: %s\n", fire->exec);
				}
				keystack_clean(&current_keyevent.stack);
			} else {
				//printf("%s\n", KEY_NAME[event->code]);
				if(event->code < KEY_MAX) {
					keystack_add(KEY_NAME[event->code], &current_keyevent.stack);
				}
			}
            break;
		case EV_SW:
			printf("%s : %d\n", SW_NAME[event->code], event->value);
			break;
    }
}

static void start_listener() {
    int i, fd_len, fd[MAX_LISTENER];
    fd_set fdset, initial_fdset;
    struct input_event event;
    struct timeval tv;

    FD_ZERO(&initial_fdset);
    for(i=0; i<listen_n; i++) {
        fd[i] = open(listen[i], O_RDONLY);

        if(fd[i] < 0) {
            fprintf(stderr, "input-event-daemon: open(%s): %s\n", 
                listen[i], strerror(errno));
            exit(EXIT_FAILURE);
        }
        FD_SET(fd[i], &initial_fdset);

    }

    fd_len = i;
	keystack_clean(&current_keyevent.stack);

    while(1) {
        fdset = initial_fdset;
		tv.tv_sec = 60;
		tv.tv_usec = 0;

        switch(select(fd[fd_len-1]+1, &fdset, NULL, NULL, &tv)) {
            case 0:
                printf("Timeout\n");
                break;
            case -1:
                perror("input-event-daemon: select()");
                break;
            default:
                for(i=0; i<fd_len; i++) {
                    if(FD_ISSET(fd[i], &fdset)) {
                        if(read(fd[i], &event, sizeof(event)) < 0) {
                            fprintf(stderr, "input-event-daemon: read(%s): %s\n",
                                listen[i], strerror(errno));
                            break;
                        }
                        parse_input_event(&event);
                    }
                }
                break;
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

static void config_key_event(char *shortcut, char *exec) {
	char *key;
	struct key_event *new_event;

	if(!(key_events.length < MAX_EVENTS)) {
		fprintf(stderr, "input-event-daemon: Too much key events!");
		return;
	}

	key_events.list[key_events.length] = malloc(sizeof(struct key_event));
	new_event = (struct key_event*) key_events.list[key_events.length++];

	if(new_event == NULL) {
		fprintf(stderr, "input-event-daemon: malloc() failed!");
		exit(EXIT_FAILURE);
	}

	new_event->exec = strdup(exec);
	keystack_clean(&(new_event->stack));

	key = strtok(shortcut, "+");
	while(key != NULL) {
		key = config_trim_string(key);
		keystack_add(key, &(new_event->stack));
		key = strtok(NULL, "+");
	}

	keystack_sort(&(new_event->stack));
}
static int config_switch_event();
static int config_idle_event();


static void parse_config_file() {
	FILE *config_fd;
	char buffer[512], *line;
	char *section = NULL;
	char *key, *value, *ptr;
	int line_num = 0;

	if((config_fd = fopen(config_file, "r")) == NULL) {
        fprintf(stderr, "input-event-daemon: fopen(%s): %s\n",
            config_file, strerror(errno));
		exit(EXIT_FAILURE);
	}
	
	key_events.length = 0;
	switch_events.length = 0;
	idle_events.length = 0;

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
	        fprintf(stderr, "input-event-daemon: Invalid syntax on line %d!\n", line_num);
			continue;
		}

		key = config_trim_string(key);
		value = config_trim_string(value);

		if(section == NULL) {
	        fprintf(stderr, "input-event-daemon: Missing section in config!");
			continue;
		} else if(strcmp(section, "Global") == 0) {
			if(strcmp(key, "listen") == 0) {
				if(listen_n < MAX_LISTENER) {
					listen[listen_n++] = strdup(value);
				} else {
	        		fprintf(stderr, "input-event-daemon: Too much listener!");
					continue;
				}
			} else {
	        	fprintf(stderr, "input-event-daemon: Invalid option!");
				continue;
			}
		} else if(strcmp(section, "Keys") == 0) {
			config_key_event(key, value);
		}

		
	}

	qsort(key_events.list, key_events.length,
		sizeof(char*), (int (*)(const void *, const void *)) key_event_compare);

	if(section != NULL) {
		free(section);
	}
	fclose(config_fd);
}

int main(int argc, char *argv[]) {
    //list_input_devices();

	parse_config_file();
    start_listener();

    return 0;
}
