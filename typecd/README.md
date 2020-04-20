# typecd: The Chrome OS USB Type C Daemon

## ABOUT

typecd is a system daemon for tracking the state of various USB Type C ports and connected
peripherals on a Chromebook. It interfaces with the Linux Type C connector class framework
to obtain notifications about Type C events, and to pull updated information about port and
port-partner state.
