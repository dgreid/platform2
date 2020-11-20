# tmpfiles.d Configuration Files

These `.conf` files define filesystem operations that are needed to setup paths.
This is commonly creating specific files and directories with specific
permissions and ownership prior to running a system daemon. For example an
upstart job with:

```bash
pre-start script
  mkdir -p /run/dbus
  chown messagebus:messagebus /run/dbus
  mkdir -p /var/lib/dbus
end script
```

Can be replaced with a `tmpfiles.d` file with:

```bash
d /run/dbus 0755 messagebus messagebus
d /var/lib/dbus/ 0755 root root
```

This file should have the `.conf` extension and be installed to
`/usr/lib/tmpfiles.d` For more information about the `conf` format see the
[upstream documentation](https://www.freedesktop.org/software/systemd/man/tmpfiles.d.html).
