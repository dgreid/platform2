# Ippusb Manager

`ippusb_manager` provides support for IPP-over-USB printing in Chrome
OS. This is jointly achieved with
[CUPS](https://chromium.googlesource.com/chromiumos/third_party/cups/)
and with
[ippusbxd][ippusbxd].

In addition, `ippusbxd` is partially documented here.

[TOC]

## Overview

![ippusb_manager overview diagram](./overview_diagram.png)

* The entities involved:
  * `CUPS` - the print spooler that wants to print via IPP-over-USB.
  * `ippusb_manager` - service that helps establish communication
    between the print spooler and `ippusbxd`. Developed specifically
    for IPP-over-USB printing in Chromium OS.
  * `ippusbxd` - service that facilitates IPP-over-USB communication.
    Heavily patched for use in Chromium OS. Known to differ
    substantially from upstream.

## Closer look: CUPS

* The print queue URI format used for IPP-over-USB printing is
  `ippusb://<VID>_<PID>/ipp/print`.
* We have patched CUPS in Chromium OS to support the
  `ippusb://` scheme.
  * These changes apply to `lpadmin` and to the `ipp` backend.
  * If a printer uses the `ippusb://` scheme, then CUPS sends a request
    to `ippusb_manager` to broker a socket for communication with
    the printer by way of `ippusbxd`.
* The usage of the `ippusb://` scheme is specific to Chromium OS.
  * As this is a non-standard extension, printers don't know how to
    respond to this.
  * We retain the `ippusb://` scheme in CUPS configuration etc.
    on-device, but rewrite the URI to use the `ipp://` scheme before
    communicating with the printer.

## Closer look: ippusb\_manager

* Negotiates initial communication between `CUPS` and `ippusbxd`.
* Runs on demand.
  * [Started by upstart-socket-bridge][upstart-socket-bridge-conf]
    when data is written to `/run/ippusb/ippusb_manager.sock`.
* Receives a query from CUPS requesting a printer of a given
  VID and PID.
  * Searches for a printer matching given VID and PID.
  * Search outcome is determined
    * by
      [the presence of sockets exposed by ippusbxd][ippusbxd-sockets],
      `/run/ippusb/<VID>_<PID>.sock` and
      `/run/ippusb/<VID>_<PID>_keep_alive.sock`, and
    * by the liveness of `ippusbxd`, checked by
      [sending a keep-alive message on the latter socket][manager-sending-keep-alive].
* If necessary, [spawns an instance of ippusbxd][manager-spawning-xd].
* Responds to `CUPS` with the basename of the socket over which
  it can communicate with `ippusbxd`.

## Closer look: keep-alive messages

* These messages are a feature specific to Chromium OS.
* Sent from `ippusb_manager` to `ippusbxd` to preempt the latter
  from its timed idle exit.
* `ippusb_manager` listens for an explicit acknowledgement before
  declaring `ippusbxd` alive and reusing the extant sockets.
* If no acknowledgement comes, `ippusb_manager` tries to wait for the
  sockets to disappear (i.e. that `ippusbxd` has exited) before
  spawning a new instance of `ippusbxd`.

## Closer look: ippusbxd

* Is started by `ippusb_manager`, as mentioned above.
* Is always waiting to exit when idle.
  * Exits when
    [10 seconds have elapsed without activity][ippusbxd-timed-exit].
  * If `ippusbxd` receives a keep-alive from `ippusb_manager`,
    [it resets the idle timer][ippusb-idle-bump] in anticipation of
    imminent activity.
* Blindly shuttles IPP messages between `CUPS` and the printer.
  * Allocates a pair of threads for every available `ippusb` interface
    on a given printer: one for each direction of communication.
  * Does not inspect contents of messages passed.
  * There are no "turns" or state; both threads poll continuously for
    data and pass it along as quickly as they are able.
* Holds all available `ippusb` interfaces on the printer until exit.
  * Some printers (e.g. the Canon DX570) are known to behave erratically
    if we attempt to release interfaces when we were done with them.
* Patched in Chromium OS to use
  [UNIX domain sockets][ippusbxd-unix-sockets-patch]
  rather than network sockets.

## Appendix: minijail usage

* Both `ippusb_manager` and `ippusbxd` are run from inside minijail
  instances and retain their own seccomp filters.
* Forked processes inherit seccomp policies from their parents. Since
  `ippusb_manager` forks `ippusbxd`, the former seccomp filter must be
  a superset of the latter.

## Appendix: Q&A

*How much of ippusbxd do we use?*

We don't use any of the avahi code.

*(WRT ippusb_manager) why Unix sockets; why not D-Bus?*

We didn't consider D-Bus at the time. The messages are
quite simple and `CUPS` already had code for dealing with Unix sockets.

*Can a user plug in more than one USB printer?*

Yes, as long as they do not appear to be the same (i.e. present
identical VIDs and PIDs). We opine that this is an uncommon enough
use case to be an issue.

*Can ippusb_manager and ippusbxd have multiple clients?*

Preliminary testing indicates that this should work. Sockets are
connection-oriented.

## Internal Documentation

See the [design doc](http://go/ipp-over-usb) for information about the overall
design and how `ippusb_manager` fits into it. This is accessible only within
google.

## Code Overview

This repository contains the following subdirectories:

| Subdirectory | Description |
|--------------|-------------|
| `etc/init`   | Upstart config files for launching `ippusb_manager` |
| `udev/`      | udev rules for setting group permissions on ipp-usb printers |

[ippusbxd]: https://chromium.googlesource.com/chromiumos/overlays/chromiumos-overlay/+/refs/heads/master/net-print/ippusbxd/
[upstart-socket-bridge-conf]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/master:src/platform2/ippusb_manager/etc/init/ippusb.conf;l=9;drc=6ba85eed862624662a909d840faa1b76bda2f8b5
[ippusbxd-sockets]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/master:src/platform2/ippusb_manager/ippusb_manager.cc;l=221;drc=bf4b7828dc29e25bf036924c51ac2072ab13ba7a
[manager-sending-keep-alive]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/master:src/platform2/ippusb_manager/ippusb_manager.cc;l=233;drc=bf4b7828dc29e25bf036924c51ac2072ab13ba7a
[manager-spawning-xd]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/master:src/platform2/ippusb_manager/ippusb_manager.cc;l=244;drc=bf4b7828dc29e25bf036924c51ac2072ab13ba7a
[ippusbxd-timed-exit]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/master:src/third_party/chromiumos-overlay/net-print/ippusbxd/files/keep-alive.patch;l=123;drc=b50344ddb467d9587da16290837c6c80200b1c2f
[ippusb-idle-bump]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/master:src/third_party/chromiumos-overlay/net-print/ippusbxd/files/keep-alive.patch;l=324;drc=b50344ddb467d9587da16290837c6c80200b1c2f
[ippusbxd-unix-sockets-patch]: https://source.chromium.org/chromiumos/chromiumos/codesearch/+/master:src/third_party/chromiumos-overlay/net-print/ippusbxd/files/unix-socket.patch;l=6;drc=085f54c7e3fc3d82db4a3bf250f49c1c3f7c37eb
