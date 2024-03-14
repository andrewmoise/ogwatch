/*

Copyright (C) 2013, Heinrich Schuchardt <xypron.glpk@gmx.de>
Copyright (C) 2014, Michael Kerrisk <mtk.manpages@gmail.com>
Copyright (C) 2024, Andrew Moise <andrew.moise@gmail.com>

(Derived from Linux man pages fanotify(7) man page example)

Licensed under GNU Affero General Public License, Version 3

*/

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fanotify.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "ogwatch.h"

#define BUF_SIZE 256
#define ESTALE_DEBOUNCE_DELAY 50

// Define the full list of fanotify events
static EventMap fanotify_events[] = {
    {"FAN_CREATE", FAN_CREATE},
    {"FAN_MOVED_TO", FAN_MOVED_TO},
    {"FAN_OPEN", FAN_OPEN},
    {"FAN_ACCESS", FAN_ACCESS},
    {"FAN_MODIFY", FAN_MODIFY},
    {"FAN_CLOSE_WRITE", FAN_CLOSE_WRITE},
    {"FAN_CLOSE_NOWRITE", FAN_CLOSE_NOWRITE},
    {"FAN_MOVED_FROM", FAN_MOVED_FROM},
    {"FAN_DELETE", FAN_DELETE},
    {NULL, 0}
};

// Implementations
EventMap *get_full_events_list() {
    return fanotify_events;
}

unsigned int get_default_file_events_mask() {
    return FAN_CREATE | FAN_DELETE | FAN_MOVED_FROM | FAN_MOVED_TO | FAN_CLOSE_WRITE;
}

unsigned int get_default_dir_events_mask() {
    return FAN_CREATE | FAN_DELETE | FAN_MOVED_FROM | FAN_MOVED_TO;
}

unsigned int get_generic_file_events_mask() {
    return FAN_DELETE | FAN_MOVED_FROM | FAN_MOVED_TO | FAN_CLOSE_WRITE;
}

unsigned int get_generic_dir_events_mask() {
    return FAN_CREATE | FAN_DELETE | FAN_MOVED_FROM | FAN_MOVED_TO;
}

int access_is_ok(uid_t real_uid, uid_t effective_uid, const char *path) {
    struct stat statbuf;
    int result;

    // Drop privileges
    if (seteuid(real_uid) == -1) {
        perror("Failed to drop privileges");
        exit(EXIT_FAILURE);
    }
    
    if (lstat(path, &statbuf) == -1) {
        if (errno == EACCES) {
            result = 0;
        } else if (errno == ENOENT) {
            result = 1;
        } else {
            perror("stat failed");
            exit(EXIT_FAILURE);
        }
    } else {
        result = 1;
    }

    // Restore privileges
    if (seteuid(effective_uid) == -1) {
        perror("Failed to restore privileges");
        exit(EXIT_FAILURE);
    }

    return result;
}

// Debouncing for ESTALE messages
int should_print_estale(int fd, struct timeval *estale_timestamp) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    struct timeval now, timeout;
    gettimeofday(&now, NULL);

    long elapsed = (now.tv_sec - estale_timestamp->tv_sec) * 1000 + (now.tv_usec - estale_timestamp->tv_usec) / 1000;
    long remaining = ESTALE_DEBOUNCE_DELAY - elapsed;

    if (remaining > 0) {
        timeout.tv_sec = remaining / 1000;
        timeout.tv_usec = (remaining % 1000) * 1000;
    } else {
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
    }

    /* Note - by design this will also not print ESTALE while there are messages waiting to read */
    int res = select(fd + 1, &fds, NULL, NULL, &timeout);
    if (res <= 0) {
        *estale_timestamp = now;
        return 1;
    } else {
        return 0;
    }
}

void event_watch_loop(const char *watch_path, unsigned int file_events_mask, unsigned int dir_events_mask, int generic_mode, char terminator) {
    int fd, ret, event_fd, mount_fd;
    ssize_t len, path_len;
    char path[PATH_MAX];
    char procfd_path[PATH_MAX];
    char events_buf[BUF_SIZE];
    struct file_handle *file_handle;
    struct fanotify_event_metadata *metadata;
    struct fanotify_event_info_fid *fid;
    const char *file_name;
    struct stat sb;

    mount_fd = open(watch_path, O_DIRECTORY | O_RDONLY);
    if (mount_fd == -1) {
        perror(watch_path);
        exit(EXIT_FAILURE);
    }

    /* Create an fanotify file descriptor with FAN_REPORT_DFID_NAME as
       a flag so that program can receive fid events with directory
       entry name. */

    fd = fanotify_init(FAN_CLASS_NOTIF | FAN_REPORT_DFID_NAME | FAN_UNLIMITED_QUEUE, 0);
    if (fd == -1) {
        perror("fanotify_init");
        exit(EXIT_FAILURE);
    }

    if (file_events_mask != 0) {
        ret = fanotify_mark(fd, FAN_MARK_ADD | FAN_MARK_FILESYSTEM,
			    file_events_mask | FAN_EVENT_ON_CHILD,
                            AT_FDCWD, watch_path);
        if (ret == -1) {
            perror("fanotify_mark");
            exit(EXIT_FAILURE);
        }
    }

    if (dir_events_mask != 0) {
        ret = fanotify_mark(fd, FAN_MARK_ADD | FAN_MARK_FILESYSTEM,
			    dir_events_mask | FAN_ONDIR,
                            AT_FDCWD, watch_path);
        if (ret == -1) {
            perror("fanotify_mark");
            exit(EXIT_FAILURE);
        }
    }

    uid_t real_uid = getuid();
    uid_t effective_uid = geteuid();

    struct timeval estale_timestamp;
    gettimeofday(&estale_timestamp, NULL);
    int estale_pending = 0;

    while(1) {
        /* Print an ESTALE if we have one, subject to debouncing */
        if (estale_pending) {
            if (should_print_estale(fd, &estale_timestamp)) {
                if (!generic_mode)
                    printf("ESTALE\n");
                estale_pending = 0;
            }
        }

        /* Read events from the event queue into a buffer. */
        len = read(fd, events_buf, sizeof(events_buf));
        if (len == -1 && errno != EAGAIN) {
            perror("read");
            exit(EXIT_FAILURE);
        }

        /* Process all events within the buffer. */

        for (metadata = (struct fanotify_event_metadata *) events_buf;
                FAN_EVENT_OK(metadata, len);
                metadata = FAN_EVENT_NEXT(metadata, len)) {
            fid = (struct fanotify_event_info_fid *) (metadata + 1);
            file_handle = (struct file_handle *) fid->handle;

            /* Ensure that the event info is of the correct type. */

            if (fid->hdr.info_type == FAN_EVENT_INFO_TYPE_FID ||
                fid->hdr.info_type == FAN_EVENT_INFO_TYPE_DFID) {
                file_name = NULL;
            } else if (fid->hdr.info_type == FAN_EVENT_INFO_TYPE_DFID_NAME
                || fid->hdr.info_type == FAN_EVENT_INFO_TYPE_OLD_DFID_NAME
                || fid->hdr.info_type == FAN_EVENT_INFO_TYPE_NEW_DFID_NAME)
            {
                file_name = file_handle->f_handle +
                            file_handle->handle_bytes;
            } else {
                printf("Skipping info type %d\n", fid->hdr.info_type);
                continue;
            }

            /* metadata->fd is set to FAN_NOFD when the group identifies
            objects by file handles.  To obtain a file descriptor for
            the file object corresponding to an event you can use the
            struct file_handle that's provided within the
            fanotify_event_info_fid in conjunction with the
            open_by_handle_at(2) system call.  A check for ESTALE is
            done to accommodate for the situation where the file handle
            for the object was deleted prior to this system call. */

            event_fd = open_by_handle_at(mount_fd, file_handle, O_RDONLY);
            if (event_fd == -1) {
                if (errno == ESTALE) {
                    estale_pending = 1;
                    continue;
                } else {
                    perror("open_by_handle_at");
                    exit(EXIT_FAILURE);
                }
            }

            snprintf(procfd_path, sizeof(procfd_path), "/proc/self/fd/%d",
                    event_fd);

            /* Retrieve and print the path of the modified dentry. */

            path_len = readlink(procfd_path, path, sizeof(path) - 1);
            if (path_len == -1) {
                perror("readlink");
                exit(EXIT_FAILURE);
            }
            path[path_len] = 0;

            /* Do our cleanup now, so we can just 'continue' in order not to print */

            if (close(event_fd) == -1) {
                perror("close");
                exit(EXIT_FAILURE);
            }

            /* Check that we're in the watched subdir */

            if (strncmp(path, watch_path, strlen(watch_path)) != 0)
                continue;

            /* Check that we have access to the location of the event */

            int full_path_len = strlen(path) + 1;
            if (file_name != NULL)
                full_path_len += strlen(file_name) + 1;
            
            char full_path[full_path_len];
            if (file_name != NULL)
                snprintf(full_path, full_path_len, "%s/%s", path, file_name);
            else
                snprintf(full_path, full_path_len, "%s", path);

            if (!access_is_ok(real_uid, effective_uid, full_path))
                continue;

            /* We passed the checks, print events */

            if (!generic_mode) {
                const char *dir_or_file = (metadata->mask & FAN_ONDIR) ? "|FAN_ONDIR" : "";
                for (int i = 0; fanotify_events[i].name != NULL; i++) {
                    if (metadata->mask & fanotify_events[i].value) {
                        printf("%s%s %s/%s%c", fanotify_events[i].name, dir_or_file, path, file_name, terminator);
                        fflush(stdout); // Ensure immediate output
                    }
                }
            } else {
                printf("%s/%s%c", path, file_name, terminator);
                fflush(stdout); // Ensure immediate output
            }
        }
    }
}
