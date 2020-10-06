# Chrome OS Suspend and Resume

## Overview

This flowchart depicts the process that powerd's `Suspender` class follows to
suspend and resume a Chrome OS system:

![suspend flowchart](images/suspend_flowchart.png)

Yellow boxes indicates states where powerd's event loop is running, white ovals
indicate discrete tasks that are performed, and arrows indicate events or other
occurrences that result in changes to the state.

The process begins when powerd (or some other process) decides that the system
should be suspended. Suspender receives a request to suspend the system and
generates an ID corresponding to the request. It performs pre-suspend
preparation, such as muting audio and turning off the panel backlight. Then, it
emits a `SuspendImminent` D-Bus signal to announce the suspend request to other
processes (particularly ones that have registered [suspend delays]). The
`SuspendDelayController` class is used to track the state of suspend delays.

After all suspend delays have become ready or have timed out, powerd starts a
suspend attempt. This involves using `powerd_setuid_helper` to run the
`powerd_suspend` script as root; this script is responsible for writing `mem` to
`/sys/power/state` (the actual act that tells the kernel to suspend). If
`powerd_suspend` reports that the system suspended and resumed successfully,
then the pre-suspend preparations are undone, a `SuspendDone` D-Bus signal is
emitted to inform other process that the suspend request is complete, and
`Suspender` is ready for the next suspend request.

If the suspend attempt fails, `Suspender` waits for ten seconds before running
`powerd_suspend` again to retry. After ten failed retries, the system is shut
down.

## Display Suspend State

The display state for suspend is controlled by Chrome for suspend.

-  When the SuspendImminent signal is received by Chrome, it turns off the
   display(s).
-  powerd still controls the backlight level during suspend, even though
   powering off the display will disable the backlight separately.
-  Chrome will turn the display(s) back on when it receives the SuspendDone
   signal.

## Wakeup Counts

The kernel provides a `[/sys/power/wakeup_count]` file that can be used to avoid
races while suspending. Drivers within the kernel increment this count in
response to user activity like keyboard or touchpad events.

-   When powerd receives a suspend request, it reads and saves the current
    count.
-   When the `powerd_suspend` script is run to suspend the system, it writes the
    previously-read count back to the `wakeup_count` file. If the number that it
    writes doesn't match the kernel's current count, the kernel reports failure,
    which results in `powerd_suspend` reporting failure, which results in the
    suspend attempt failing.
-   When `powerd_suspend` writes to `/sys/power/state`, the kernel again reports
    failure if the wakeup count has changed since powerd_suspend wrote to it.

This has the effect of letting suspend attempts get aborted if user activity is
observed by the kernel after the point at which the userspace powerd process
starts suspending the system.

powerd's `RequestSuspend` D-Bus method takes an optional externally-supplied
wakeup count. If a count is passed, powerd uses it instead of reading the
`wakeup_count` file. autotests exercising the suspend path typically pass a
count after installing a RTC wake alarm to trigger a resume several seconds
after suspending. If powerd sees a count mismatch, it aborts the suspend request
immediately. If it instead retried it ten seconds later, the retry would
probably occur after the wake alarm had fired, resulting in the system sleeping
forever.

## Dark Resume

On some systems, powerd passes a duration to the kernel in order to periodically
wake the system from the S3 state. This results in an
awake-with-the-display-off-and-audio-muted mode referred to as "dark resume".
After the system wakes into dark resume, powerd checks the battery level. If it
is low enough to suggest that the battery will be drained entirely while in S3,
resulting in a system that can't be used until it's recharged, powerd shuts down
the system. Otherwise, it re-suspends immediately.

## Enable console during suspend

Enabling console messages during suspend can help in debugging kernel panics
during suspend. By default console is enabled during system suspends to S3. But
if S0iX is enabled, console is disabled by default as console activity on the
UART can prevent system from suspending. To enable console temporarily for
debugging S0iX, perform the following steps

```sh
# echo 1 > /var/lib/power_manager/enable_console_during_suspend
# restart powerd
```

## Configuring suspend mode

Please look at [power states] documentation for info on suspend modes. If
`suspend_to_idle` preference is set then suspend mode is defaulted to 's2idle'.
Otherwise suspend mode can be configured using `suspend_mode` preference.
Currently valid suspend modes are `s2idle`, `shallow` and `deep`. To configure
suspend mode temporarily for debugging, perform the following steps

```sh
# echo mode > /var/lib/power_manager/suspend_mode
# restart powerd
```

Note that if suspend_to_idle is enabled, `suspend_mode` preference will not
matter.

## Firmware Updates

powerd will avoid suspending (or shutting down) if it believes that the firmware
is being updated. After seeing the presence of a lockfile in
`/run/lock/power_override`, powerd will act as if the suspend attempt failed and
retry it later. Files in this directory are created by various programs
including `flashrom` and `battery_tool`. There are additional details on the
[flashrom] page.

[suspend delays]: https://chromium.googlesource.com/chromiumos/platform/system_api/+/master/dbus/power_manager/suspend.proto
[/sys/power/wakeup_count]: https://lwn.net/Articles/393314/
[flashrom]: https://dev.chromium.org/chromium-os/packages/cros-flashrom
[power states]: https://www.kernel.org/doc/Documentation/power/states.txt
