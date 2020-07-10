# Chrome OS ARC data snapshotd daemon

This package implements arc-data-snapshotd, a running in minijail daemon that
executes operations with ARC snapshots of /data directory for Managed Guest
Session (MGS) requested by Chrome browser.

The arc-data-snapshotd interface is exposed to Chrome browser through a D-Bus
API.
