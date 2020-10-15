[TOC]

# Crosh -- The Chromium OS shell

This is the homepage/documentation for the crosh, the Chromium OS shell.
If you're on a CrOS devices right now, you should be able to launch crosh
by hitting Ctrl+Alt+T.  If you aren't on CrOS, then most likely that won't
do anything useful :).

# For Users

Just run `help` to get info about available commands and discover more.

You can also use tab completion to quickly find existing commands.

It's an adventure!

# For Chromium OS Developers

This section is meant for people hacking on Chromium OS, especially when they
need to modify/extend crosh.

## Security Warning

Please do not install new modules without full security review.  Any insecure
code that crosh loads will be directly available to people in verified mode.
That's an easy attack vector to run arbitrary code and mess with the user's
state.  We don't want to undermine the security of CrOS!

If you are looking for reviewers, look at the [./OWNERS] file.

## Where Files Live

The main [`crosh`](./crosh) script should hold the core crosh logic only.  All
other functions should live in appropriate module directories.

### Source Repos

Modules that are specific to a board, or heavily specific to a package, should
generally live with that board and/or package.  For functions that are always
available on all CrOS devices, that code should be kept in this repo.

If you're unsure, just ask on chromium-os-dev@chromium.org.

### Runtime Filesystem

Module directories live in subdirectories under `/usr/share/crosh/` on the
CrOS device.  This list is in order -- so modules in a later dir may override
earlier ones.

* [`extra.d/`](./extra.d/): Modules always loaded.
* [`removable.d/`](./removable.d/): Modules loaded when running on a removable
  device (e.g. USB).
* [`dev.d/`](./dev.d/): Modules only loaded when the device is in dev mode.

Only modules using the name format `##-<name>.sh` will be loaded.  The leading
numbers are used to control ordering and the `.sh` suffix is a validity check.

As an example, `crosh` stores its dev mode module at
`/usr/share/crosh/dev.d/50-crosh.sh` and its removable drive module at
`/usr/share/crosh/removable.d/50-crosh.sh`.

If you don't have a need for a specific number, just use `30`.  Don't pick a
number 50+ unless you really need to override crosh (which you rarely should).

If you don't have a name, just use one that matches your package's name.  We
use `50-crosh.sh` because the package name is `crosh`.

## Adding New Commands

For every command, you define two variables and one function.  There is no
need to register the new commands anywhere as crosh will inspect its own
runtime environment to discover them.

Here's how you would register a new `foo` command.
```sh
# A short description of arguments that this command accepts.
USAGE_foo='<some args>'
HELP_foo='
  Extended description of this command.
'
# Not required, but lets crosh detect if the foo program is available in the
# current system (e.g. the package is not installed on all devices).  If it
# isn't available, crosh will automatically display an error message and never
# call cmd_foo.
EXEC_foo='/full/path/to/program'
cmd_foo() (
  # Implementation for the foo command.
  # You should validate $# and "$@" and process them first.
  # For invalid args, call the help function with an error message
  # before returning non-zero.
  ...foo code goes here!...
)
```

See the design section below for more details on what and how to structure
the new command.

### Command Help

If your crosh command simply calls out to an external program to do the
processing, and that program already offers usage details, you probably
don't want to have to duplicate things.  You can handle this scenario by
defining a `help_foo` function that makes the respective call.

```sh
# Set the help string so crosh can discover us automatically.
HELP_foo=''
cmd_foo() (
  ...
)
help_foo() (
  /some/command --help
)
```

Take note that we still set `HELP_foo`.  This is needed so crosh can discover
us automatically and display us in the relevant user facing lists (like the
`help_advanced` command).  We don't need to set `USAGE_foo` though since the
`help_foo` function does that for us.

## Hiding Commands

If a command is not yet ready for "prime time", you might want to have it in
crosh for early testing, but not have it show up in the `help` output where
users can easily discover it (of course, the code is all public, so anyone
reading the actual source can find it).  Here's how you do it.

```sh
# Set the vars to pass the unittests ...
USAGE_vmc=''
HELP_vmc=''
# ... then unset them to hide the command from "help" output.
unset USAGE_vmc HELP_vmc
cmd_vmc() (
  ...
)
```

## Deprecating Commands

If you want to replace a crosh command with some other UI (like a chrome://
page), and you want to deprecate the command gracefully by leaving behind a
friendly note if people try to use it, here's the form.

```sh
# Set the vars to pass the unittests ...
USAGE_storage_status=''
HELP_storage_status=''
# ... then unset them to hide the command from "help" output.
unset USAGE_storage_status HELP_storage_status
cmd_storage_status() (
  # TODO: Delete this after the R## release branch.
  echo "Removed. See storage_info section in chrome://system"
)
```

Make sure you add the TODO comment so people know in the future when it's OK
to clean it up.

## Testing

### Iterative Development

You can run `./crosh` on your desktop system to get a sample shell.  You can
quickly test basic interactions (like argument parsing) here, or check the
help output.  You won't have access to the CrOS services that many crosh
commands expect to talk to (via D-Bus), so those commands will fail.

If you want to load dev mode modules, you can use `./crosh --dev`.  It will
only load local modules ([`./dev.d/`](./dev.d/)), so if your module lives
elsewhere, you can copy it here temporarily.

Similarly, if you want to load removable device modules, you can use
`./crosh --removable`.

### Unittests

The [`./run_tests.sh`](./run_tests.sh) unittest runner performs a bunch of
basic style and soundness checks.  Run it against your code!

## Command Design

The crosh shell runs in the same environment as the browser (same user/group,
same Linux namespaces, etc...).  So any tools you run in crosh, or information
you try to acquire, must be accessible to the `chronos` user.

However, we rarely want crosh to actually execute tools directly.  Instead,
you should add D-Bus callbacks to the debugd daemon and send all requests to
it.  We can better control access in debugd and lock tools down.  Then the
only logic that exists in crosh is a D-Bus IPC call and then displays output
from those programs.  Discussion of debugd is out of scope here, so check out
the [/debugd/](/debugd/) directory instead.

# Future Work (TODO)

Anyone should feel free to pick up these ideas and try to implement them :).

* Move any remaining commands that are implemented in place to debugd calls
  so they can be done over D-Bus.
* Run crosh itself in a restricted sandbox (namespaces/seccomp/etc...).
  Once all commands are done via IPC, there's no need to keep privs.
  Might make it dependent upon dev mode though so we don't break `shell`.
