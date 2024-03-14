# ogwatch

## What it is

`ogwatch` is a cross-platform utility designed to simplify recursive file system event monitoring. It recursively monitors an entire directory tree and reports events on its stdout. It aims to provide a more straightforward, secure, and resource-efficient method for file system event monitoring compared to existing solutions.

We can filter individually for certain types of events that apply to either files or directories under the watch directory. If no event filters are provided, we will default to a sensible subset of events that still gives a picture of what's going on.

Event types for filtering and reporting are implementation-dependent -- we do not attempt to translate event types into an implementation-independent set. However, you can specify `-g` to request a purely implementation-independent mode that simply reports which paths have changed, and lets you scan the files in question to discover what has changed (which generally speaking you have to do anyway, because of race conditions).

At present, it supports MacOS and GNU/Linux.

## Pre alpha

This is in early stages of development. It has been made public for testing, feedback, and contributions. While it has been designed with care, it has not been extensively tested in production environments. Please proceed with caution and report any issues or suggestions for improvement.

## Example

```bash
ogwatch /path/to/watch
```

Output (for the fanotify backend) looks something like this:

```
FAN_CREATE /home/user/ogwatch/.git/refs/heads/main.lock
FAN_CLOSE_WRITE /home/user/ogwatch/.git/refs/heads/main.lock
FAN_CLOSE_WRITE /home/user/ogwatch/.git/logs/HEAD
FAN_CLOSE_WRITE /home/user/ogwatch/.git/logs/refs/heads/main
FAN_MOVED_FROM /home/user/ogwatch/.git/refs/heads/main.lock
FAN_MOVED_TO /home/user/ogwatch/.git/refs/heads/main
FAN_DELETE /home/user/ogwatch/.git/HEAD.lock
FAN_CREATE /home/user/ogwatch/.git/objects/maintenance.lock
FAN_CLOSE_WRITE /home/user/ogwatch/.git/objects/maintenance.lock
FAN_DELETE /home/user/ogwatch/.git/objects/maintenance.lock
```

Events applying to directories will have a directory flag suffixed to them:

```
FAN_MOVED_FROM|FAN_ONDIR /home/user/grits/ogwatch
FAN_MOVED_TO|FAN_ONDIR /home/user/ogwatch-2
```

Or, in generic (`-g`) mode:

```
/home/user/ogwatch/.git/refs/heads/main.lock
/home/user/ogwatch/.git/refs/heads/main.lock
/home/user/ogwatch/.git/logs/HEAD
/home/user/ogwatch/.git/logs/refs/heads/main
/home/user/ogwatch/.git/refs/heads/main.lock
/home/user/ogwatch/.git/refs/heads/main
/home/user/ogwatch/.git/HEAD.lock
/home/user/ogwatch/.git/objects/maintenance.lock
/home/user/ogwatch/.git/objects/maintenance.lock
/home/user/ogwatch/.git/objects/maintenance.lock
```

### Options

* -f: File events to monitor, as a comma-separated list of (backend-specific) event types.
* -d: Directory events to monitor, as a comma-separated list of (backend-specific) event types.

* -g: Run in "generic" mode -- don't report event types, simply report changed paths.
* -0: Use null characters as terminators in the output, instead of newlines.
* -h: Display help text and exit.

Events applying to files or to directories are selected individually, using the -f and -d options respectively. If no options are specified, it will default to a limited list which will still let you know if files or file contents are changing.

In most cases, you can use "-g" with no event filters, and then simply examine for yourself the paths you get to discover what has happened to the filesystem. There's an inherent race conditiong anyway between when the event is created and when you're looking for the file the event corresponds to -- so it is perfectly safe simply to ignore the event type, and rescan for yourself the paths provided and update the state of your application accordingly.

Note that if you're doing this, and you receive an existing directory as a path, you must rescan the entire directory recursively to be guaranteed that you'll receive all updates. 

On queue overruns where the kernel may have lost events, we emit the root of the watch directory to request a full rescan, since that's the only way to guarantee up-to-date-ness in that case.

### Types of events (fanotify)

If you are specifying arguments to `-d` or `-f`, or parsing the output of the tool, these are the event types you'll see:

* FAN_CREATE - File or directory created
* FAN_MOVED_TO - File or directory moved away
* FAN_OPEN - File opened by process
* FAN_ACCESS - File accessed by process
* FAN_MODIFY - File modified by process (NOTE: You probably want FAN_CLOSE_WRITE instead of this, to debounce multiple writes into one event, and to only get the event once all modifications by the process are finished with.)
* FAN_CLOSE_WRITE - File closed after modifications were made
* FAN_CLOSE_NOWRITE - File closed after no modifications
* FAN_MOVED_FROM - File moved into this location
* FAN_DELETE - File deleted
* ESTALE - File was changed but then removed before we could see it

Events corresponding to directories will have FAN_ONDIR appended to them, e.g. `FAN_MOVED_TO|FAN_ONDIR`.

Note that you will receive FAN_MOVED_TO and FAN_MOVED_FROM events for files which have moved into or out of your watched tree, without any corresponding create or delete event. You must opt to monitor move events in addition to create/delete events if you want to be aware of both.
 
ESTALE happens when we get an event, but the file in question was gone before we got to look at it. In most cases, you can ignore this event, because you will get an event for a
containing directory that should cause a rescan anyway.

### Types of events (FSEvents)

Note that the FSEvents backend debounces duplicate events, with a slight delay (30 milliseconds by default).

* MustScanSubDirs: Subdirectories must be scanned due to changes that are too numerous or complex to list individually.
* UserDropped: The event was dropped due to a buffer overflow in the user space.
* KernelDropped: The event was dropped due to a buffer overflow in the kernel.
* EventIdsWrapped: Event IDs have wrapped around.

* RootChanged: The root of the monitored directory has changed.
* Mount: A filesystem was mounted under the watched directory.
* Unmount: A filesystem was unmounted under the watched directory.
* ItemCreated: A file or directory was created.
* ItemRemoved: A file or directory was removed.
* ItemInodeMetaMod: Metadata (permissions, timestamps, etc.) for a file or directory was modified.
* ItemRenamed: A file or directory was renamed.
* ItemModified: A file was modified. This can be used to monitor file content changes.
* ItemFinderInfoMod: Finder metadata for a file or directory was modified.
* ItemChangeOwner: The owner of a file or directory was changed.
* ItemXattrMod: Extended attributes of a file or directory were modified.

* ItemIsFile: The event pertains to a file.
* ItemIsDir: The event pertains to a directory.
* ItemIsSymlink: The event pertains to a symbolic link.

(ItemIsFile is filtered from the output event type; the other two are emitted as suffixes to the underlying event type.)

## Installation

It's not complex.

### Linux

Compile the C program:

```bash
gcc fanotify.c main.c -o ogwatch
```

Move the compiled binary to a suitable location (e.g., /usr/local/bin) and make it setuid:

```bash
sudo chown root:root ogwatch
sudo chmod u+s ogwatch
sudo mv ogwatch /usr/local/bin/
```

#### Security Considerations

The Linux binary must run setuid, since we cannot efficiently monitor a directory recursively without CAP_SYS_ADMIN.

We take precautions to not introduce any vulnerabilities, by checking access permissions and only reporting files that the current user has access to. However, the whole thing runs as root, so as with any privileged tool that is user-configurable, users are encouraged to review the security implications of its use in their specific context.

It might not be a bad idea for production use to make it *only* runnable or readable by the application's user, tucked away somewhere.

### MacOS

```
gcc fsevents.c main.c -o ogwatch -framework CoreServices
sudo chown root ogwatch
sudo mv ogwatch /usr/local/bin/
```

## Contributions!

Feedback, bug reports, and contributions are highly encouraged. Hope this is useful for you.
