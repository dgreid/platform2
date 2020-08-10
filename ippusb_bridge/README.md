# Ippusb Bridge

`ippusb_bridge` is a drop-in replacement for [ippusbxd][ippusbxd] for Chrome
OS. It provides support for connecting to IPP-over-USB printers, and
forwarding arbitrary protocols (IPP, eSCL, etc.) to those printers via HTTP.

The impetus for creating this was that ippusbxd has lost a lot of interest
since the release of `ipp-usb`, a similar project written in Golang. Upstream
provides only some oversight, and the code is pure C and has security bugs.

Preferably, we would use `ipp-usb` on Chrome OS instead, but its binary size
(~9MB) precludes it.

Thus, ippusb_bridge is the happy medium: written in a safe language, to avoid
exposing unsafe code to all USB devices on the system, but still with a
reasonable binary size.

[ippusbxd]: https://www.github.com/OpenPrinting/ippusbxd
