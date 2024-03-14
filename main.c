/*

Copyright (C) 2024, Andrew Moise <andrew.moise@gmail.com>

Licensed under GNU Affero General Public License, Version 3

*/

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "ogwatch.h"

// Parses the event names from the command line arguments and returns the corresponding fanotify mask.
unsigned int parse_events(char *events_str) {
    EventMap *events = get_full_events_list();
    unsigned int mask = 0;
    char *event_name = strtok(events_str, ",");
    while (event_name != NULL) {
        for (int i = 0; events[i].name != NULL; i++) {
            if (strcmp(events[i].name, event_name) == 0) {
                mask |= events[i].value;
                break;
            }
        }
        event_name = strtok(NULL, ",");
    }

    return mask;
}

// Help message function
void print_help() {
    printf("Usage: ogwatch [options] <directory>\n");
    printf("Options:\n");
    printf("  -f <file_events>   Comma-separated list of file events to see.\n");
    printf("  -d <dir_events>    Comma-separated list of directory events to see.\n");
    printf("  -0                 Use null character as terminator for output lines.\n");
    printf("  -g                 Enable generic output mode, printing only paths.\n");
    printf("  -h                 Display this help message and exit.\n");
    printf("\nEvents:\n");
    printf("The events you can monitor are specific to the backend in use. On this\n");
    printf(" platform, they are:\n");
    printf("\n");
    EventMap *events = get_full_events_list();
    for (int i = 0; events[i].name != NULL; i++) {
        printf("  %s\n", events[i].name);
    }
}

int main(int argc, char *argv[]) {
    int fd, ret, event_fd, mount_fd;
    ssize_t len, path_len;
    char path[PATH_MAX];
    char procfd_path[PATH_MAX];
    struct file_handle *file_handle;
    struct fanotify_event_metadata *metadata;
    struct fanotify_event_info_fid *fid;
    const char *file_name;
    struct stat sb;
    const char *watch_path;
    unsigned int file_events_mask = 0, dir_events_mask = 0;
    int generic_mode = 0;
    int opt;
    char terminator = '\n';

    while ((opt = getopt(argc, argv, "f:d:0gh")) != -1) {
        switch (opt) {
            case 'f':
                file_events_mask = parse_events(optarg);
                break;
            case 'd':
                dir_events_mask = parse_events(optarg);
                break;
            case 'g':
                generic_mode = 1;
                break;
            case '0':
                terminator = '\0';
                break;
            case 'h':
                print_help();
                exit(EXIT_SUCCESS);
            default:
                print_help();
                exit(EXIT_FAILURE);
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "Missing path argument. Use -h for help.\n");
        exit(EXIT_FAILURE);
    }

    if (!file_events_mask && !dir_events_mask) {
        if (!generic_mode) {
            file_events_mask = get_default_file_events_mask();
            dir_events_mask = get_default_dir_events_mask();
        } else {
            file_events_mask = get_generic_file_events_mask();
            dir_events_mask = get_generic_dir_events_mask();
        }
    }

    event_watch_loop(argv[optind], file_events_mask, dir_events_mask, generic_mode, terminator);

    // Can't happen that we get here, we always exit from signal...
    exit(EXIT_SUCCESS);
}