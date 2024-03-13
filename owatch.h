#ifndef EVENT_WATCHER_H
#define EVENT_WATCHER_H

// A structure to hold event name and value
typedef struct {
    char *name;
    unsigned int value;
} EventMap;

// Function prototypes
EventMap *get_full_events_list();
unsigned int get_default_file_events_mask();
unsigned int get_default_dir_events_mask();
unsigned int get_generic_file_events_mask();
unsigned int get_generic_dir_events_mask();

void event_watch_loop(const char *path, unsigned int file_events_mask, unsigned int dir_events_mask, int generic_mode, char terminator);

#endif
