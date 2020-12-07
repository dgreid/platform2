# CrOS bootstat

This is the Chromium OS 'bootstat' utility.  The utility is used
to generate timestamps and other performance statistics during
system boot.

## CLI Specification

### bootstat

```sh
bootstat <event-name>
```

This command gathers and records the contents of `/proc/uptime` and disk
statistics for the boot disk (the full disk, not the boot partition), and
associates the data with the passed in `<event-name>`.

### bootstat_get_last

```sh
bootstat_get_last <event-name> [ <stat> ... ]
```

Print on standard output the value of the selected statistics recorded when
the specified event occurred.  These are the available statistics:

*   `time`: Total time since kernel startup at the time of the event.
*   `read-sectors`: Total sectors read from any partition of the boot device
    since kernel startup.
*   `write-sectors`: Total sectors written to any partition of the boot device
    since kernel startup.

If multiple statistics are requested, they are reported in order, one
per line.  If no statistics are listed on the command line, the
default is to report `time`.

If an event has occurred more than once since kernel startup, only
the statistics from the last occurrence are reported.

## API Specification

The C and C++ API is defined in [`bootstat.h`](./bootstat.h).
See that header for specification details.

## Design and Implementation Details

Uptime data are stored in a file named `/tmp/uptime-<event-name>`;
disk statistics are stored in a file named `/tmp/disk-<event-name>`.
This convention is a concession to pre-existing code that depends on
these files existing with these specific names, including the
[platform_BootPerf] test in autotest, the boot-complete upstart job,
and the Chrome code to report boot time on the login screen.

New code should treat the file names as an implementation detail,
not as the interface.  You should not add new code that depends on
the file names; instead, you should enhance the bootstat command
and/or library to provide access to the data you need.

## Code Conventions

This is currently C code, because a) the code is required for use
as a library accessible to both C and C++ code, and b) it's too
small to justify the boilerplate required to have separate C and
C++ bindings.  However, if the program grows, it may be appropriate
to convert all or part to C++.

To the extent that the code can be acceptable both to C and C++
compilers, it should also adhere to Google's C++ coding conventions.
In areas where the code is C specific, use your best judgment (note
that Google also has Objective-C coding conventions that may be
useful).

[platform_BootPerf]: https://chromium.googlesource.com/chromiumos/third_party/autotest/+/HEAD/client/site_tests/platform_BootPerf
