/*

Copyright (C) 2024, Andrew Moise <andrew.moise@gmail.com>

Licensed under GNU Affero General Public License, Version 3

*/


#include <CoreServices/CoreServices.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "ogwatch.h"

static EventMap fsevents_events[] = {
    {"None", kFSEventStreamEventFlagNone},
    {"MustScanSubDirs", kFSEventStreamEventFlagMustScanSubDirs},

    {"UserDropped", kFSEventStreamEventFlagUserDropped},
    {"KernelDropped", kFSEventStreamEventFlagKernelDropped},
    {"EventIdsWrapped", kFSEventStreamEventFlagEventIdsWrapped},
    {"HistoryDone", kFSEventStreamEventFlagHistoryDone},

    {"RootChanged", kFSEventStreamEventFlagRootChanged},
    {"Mount", kFSEventStreamEventFlagMount},
    {"Unmount", kFSEventStreamEventFlagUnmount},
    {"ItemCreated", kFSEventStreamEventFlagItemCreated},
    {"ItemRemoved", kFSEventStreamEventFlagItemRemoved},
    {"ItemInodeMetaMod", kFSEventStreamEventFlagItemInodeMetaMod},
    {"ItemRenamed", kFSEventStreamEventFlagItemRenamed},
    {"ItemModified", kFSEventStreamEventFlagItemModified},
    {"ItemFinderInfoMod", kFSEventStreamEventFlagItemFinderInfoMod},
    {"ItemChangeOwner", kFSEventStreamEventFlagItemChangeOwner},
    {"ItemXattrMod", kFSEventStreamEventFlagItemXattrMod},

    {"ItemIsFile", kFSEventStreamEventFlagItemIsFile},
    {"ItemIsDir", kFSEventStreamEventFlagItemIsDir},
    {"ItemIsSymlink", kFSEventStreamEventFlagItemIsSymlink},
    {NULL, 0}
};

EventMap *get_full_events_list() {
    return fsevents_events;
}

unsigned int get_default_file_events_mask() {
    return kFSEventStreamEventFlagItemModified | kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemRemoved | kFSEventStreamEventFlagItemRenamed;
}

unsigned int get_default_dir_events_mask() {
    return kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemRemoved | kFSEventStreamEventFlagMount | kFSEventStreamEventFlagUnmount | kFSEventStreamEventFlagItemRenamed | kFSEventStreamEventFlagMustScanSubDirs;
}

unsigned int get_generic_file_events_mask() {
    return get_default_file_events_mask();
}

unsigned int get_generic_dir_events_mask() {
    return get_default_dir_events_mask();
}

typedef struct {
    const char *watch_path;
    int generic_mode;
    unsigned int file_events_mask;
    unsigned int dir_events_mask;
} EventWatcherContext;

void eventCallback(ConstFSEventStreamRef streamRef,
                   void *clientCallBackInfo,
                   size_t numEvents,
                   void *eventPaths,
                   const FSEventStreamEventFlags eventFlags[],
                   const FSEventStreamEventId eventIds[])
{
    char **paths = eventPaths;
    EventWatcherContext *contextData = (EventWatcherContext *)clientCallBackInfo;

    for (size_t i = 0; i < numEvents; i++) {
        const char *dir_or_file;
        unsigned int want_flags;

        if (eventFlags[i] & kFSEventStreamEventFlagItemIsDir) {
            want_flags = contextData->dir_events_mask;
            dir_or_file = "|ItemIsDir";
        } else if (eventFlags[i] & kFSEventStreamEventFlagItemIsSymlink) {
            want_flags = contextData->file_events_mask;
            dir_or_file = "|ItemIsSymlink";
        } else if (eventFlags[i] & kFSEventStreamEventFlagItemIsFile) {
            want_flags = contextData->file_events_mask;
            dir_or_file = "";
        } else {
            want_flags = ~0;
            dir_or_file = "|???";
        }

        if (!(want_flags & eventFlags[i]))
            continue;

        // Debounce double rename events
        if (i > 0
            && eventFlags[i] == eventFlags[i-1]
            && (eventFlags[i] & kFSEventStreamEventFlagItemRenamed)
            && !strcmp(paths[i-1], paths[i]))
        {
            continue;
        }

        if (!contextData->generic_mode) {
            // Iterate through each event flag
            for (int j = 0; fsevents_events[j].name != NULL; j++) {
                if (!(fsevents_events[j].value & want_flags) ||
                    fsevents_events[j].value == kFSEventStreamEventFlagItemIsDir ||
                    fsevents_events[j].value == kFSEventStreamEventFlagItemIsSymlink ||
                    fsevents_events[j].value == kFSEventStreamEventFlagItemIsFile)
                {
                    continue;
                }

                if (eventFlags[i] & fsevents_events[j].value) {
                    // Check if it's a directory or symlink for printing, based on your logic
                    printf("%s%s %s\n", fsevents_events[j].name, dir_or_file, paths[i]);
                    fflush(stdout); // Ensure immediate output
                }
            }
        } else {
            // Generic mode: just print the path
            if (eventFlags[i] & (kFSEventStreamEventFlagUserDropped | kFSEventStreamEventFlagKernelDropped)) {
                // Sadness. Queue overflowed; we just invalidate the whole directory.
                printf("%s\n", contextData->watch_path);
            } else {
                printf("%s\n", paths[i]);
            }
            fflush(stdout); // Ensure immediate output
        }
    }
}

void event_watch_loop(const char *watch_path, unsigned int file_events_mask, unsigned int dir_events_mask, int generic_mode, char terminator) {
    EventWatcherContext contextData;
    contextData.watch_path = watch_path;
    contextData.generic_mode = generic_mode;
    contextData.file_events_mask = file_events_mask;
    contextData.dir_events_mask = dir_events_mask;

    FSEventStreamContext context = {0, &contextData, NULL, NULL, NULL};
    FSEventStreamRef stream;
    CFAbsoluteTime latency = 0.03; // Latency in seconds

    CFStringRef mypath = CFStringCreateWithCString(NULL, watch_path, kCFStringEncodingUTF8);
    CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **)&mypath, 1, NULL);
    if (!pathsToWatch || !mypath) {
        fprintf(stderr, "Failed to create paths to watch\n");
        if (mypath) CFRelease(mypath);
        if (pathsToWatch) CFRelease(pathsToWatch);
        exit(EXIT_FAILURE);
    }

    // Create the event stream.
    stream = FSEventStreamCreate(NULL,
                                 &eventCallback,
                                 &context,
                                 pathsToWatch,
                                 kFSEventStreamEventIdSinceNow,
                                 latency,
                                 kFSEventStreamCreateFlagFileEvents);

    if (!stream) {
        fprintf(stderr, "Failed to create FSEvent stream\n");
        CFRelease(pathsToWatch);
        CFRelease(mypath);
        exit(EXIT_FAILURE);
    }

    // Start watching
    FSEventStreamScheduleWithRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    if (!FSEventStreamStart(stream)) {
        fprintf(stderr, "Failed to start FSEvent stream\n");
        FSEventStreamRelease(stream);
        CFRelease(pathsToWatch);
        CFRelease(mypath);
        exit(EXIT_FAILURE);
    }
 
    // Run the loop
    CFRunLoopRun();

    // Cleanup
    FSEventStreamStop(stream);
    FSEventStreamInvalidate(stream);
    FSEventStreamRelease(stream);
    CFRelease(pathsToWatch);
    CFRelease(mypath);
}

