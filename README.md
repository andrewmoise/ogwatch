# fanotify-helper

## Pre alpha

This is in early stages of development. It has been made public for testing, feedback, and contributions. While it has been designed with care, it has not been extensively tested in production environments. Please proceed with caution and report any issues or suggestions for improvement.

## What it is

`fanotify-helper` is a utility designed to simplify recursive file system event monitoring on Linux. It leverages `fanotify` to recursively monitor an entire directory tree. It aims to provide a more straightforward, secure, and resource-efficient method for file system event monitoring compared to existing solutions.

## How it works

`fanotify-helper` runs with elevated privileges to access `fanotify`'s filesystem-wide monitoring capabilities. It then filters and relays events for a specified directory tree to the user, ensuring that only relevant and visible-to-the-user events are reported. This method allows for efficient monitoring without requiring individual watches on each subdirectory.

## Example

```bash
./fanotify-helper /path/to/watch
```

This command monitors the specified path for file creation, deletion, and modification events, reporting them in real-time to stdout. You can also filter for particular types of events.

### Options

* -f: File events to monitor, as a comma-separated list.
* -d: Directory events to monitor, as a comma-separated list.
* -0: Use null characters as terminators in the output, instead of newlines.
* -h: Display help text and exit.

Events applying to files or to directories are selected individually, using the -f and -d options respectively.

If no -f or -d options are specified, we will apply:

```
-f FAN_CREATE,FAN_DELETE,FAN_MOVED_FROM,FAN_MOVED_TO,FAN_CLOSE_WRITE
-d FAN_CREATE,FAN_DELETE,FAN_MOVED_FROM,FAN_MOVED_TO
```

... which should give events pretty much whenever anything is changed.

### Types of events

* FAN_CREATE - File or directory created
* FAN_MOVED_TO - File or directory moved away
* FAN_OPEN - File opened by process
* FAN_ACCESS - File accessed by process
* FAN_MODIFY - File modified by process (NOTE: You probably want FAN_CLOSE_WRITE instead of this, to debounce multiple writes into one event, and to only get the event once all modifications by the process are finished with.)
* FAN_CLOSE_WRITE - File closed after modifications were made
* FAN_CLOSE_NOWRITE - File closed after no modifications
* FAN_MOVED_FROM - File moved into this location
* FAN_DELETE - File deleted
* ESTALE

Note that you will receive FAN_MOVED_TO and FAN_MOVED_FROM events for files which have moved into or out of your watched tree, without any corresponding create or delete event. You must opt to monitor move events in addition to create/delete events if you want to be aware of both.
 
Also note that the fanotify library will sometimes coalesce multiple events into one. If we detect multiple "simultaneous" events on the same file, we'll present them in the above order. E.g. we may see internally CREATE / MODIFY / DELETE all as one, and present them to you in that order as if they were three separate events. I do not believe that this will coalesce e.g. CREATE / DELETE / CREATE into a CREATE followed by a DELETE following by nothing, but caveat emptor.

ESTALE happens when we get an event, but the file in question was gone before we got to look at it. It is sad that this happens sometimes; for full correctness, you'll have to rescan the directory to know what happened.

## Installation

It's not complex. Compile the C program:

```bash
gcc fanotify-helper.c -o fanotify-helper
```

Move the compiled binary to a suitable location (e.g., /usr/local/bin) and make it setuid:

```bash
sudo chown root:root fanotify-helper
sudo chmod u+s fanotify-helper
sudo mv fanotify-helper /usr/local/bin/
```

## Security Considerations

We take precautions to not introduce any vulnerabilities, by checking access permissions and not following symlinks. However, the whole thing runs as root, so as with any privileged tool that is user-configurable, users are encouraged to review the security implications of its use in their specific context.

It might not be a bad idea in production to make it *only* runnable or readable by the application's user, tucked away somewhere.

## Contributions!

Feedback, bug reports, and contributions are highly encouraged. Hope this is useful for you.
