# ManaTEE Runtime Design

The runtime environment and middleware for ManaTEE as well as the API endpoints
for communicating with `sirenia` from TEEs or Chrome OS. The parent directory
for the code is named `manatee_runtime` and has 3 subdirectories.

## Sirenia

The bulk of the ManaTEE runtime code. It is named after the taxonomic family
manatees belong to. This dir provides the support for running TEE applications,
storage and crypto APIs to TEE applications, and the communication between the
application and the OS. The 2 main components of sirenia are [trichechus],
the daemon on the host machine that handles the TEE apps and interaction
between them and the guest OS, and [dugong], the daemon on the Chrome OS
guest that handles communication with the host machine and TEEs for Chrome OS.

To interact with ManaTEE:
  * From Chrome OS use [manatee-client]:
    * `dev-rust/manatee-client` for **Rust**.
    * `chromeos-base/manatee-client` for **C++**.
  * From **TEE applications** include
[`dev-rust/manatee-runtime`](#Manatee-Runtime).

### Trichechus

The TEE application life-cycle manager. It is named after the genus manatees
belong to. Trichechus is a daemon that handles bringup of TEE applications,
communication between TEE applications and Chrome OS, and all communication
between the hypervisor/host environment and the Chrome OS guest.

It serves the following purposes related to TEE applications:

1. Loading and validation
2. Instance launching and sandboxing
3. Logging and crash dump collection
4. Establishing communication
5. Instance cleanup

### Dugong

The broker daemon implementation that communicates with Trichechus. It is named
after the cousin genus to manatees since this will run on the Chrome OS guest
machine, but will be closely related to Manatee and Trichechus. Communication
to Dugong will be handled via a D-Bus handler which will then facilitate
sending requests to Trichechus on the hypervisor side.

Its roles include:

1. Validating permissions and routing requests from untrusted applications
2. Providing remote storage for Trichechus
   - application binaries
   - application data
3. Routes log events and crash reports.

### Library

Sirenia includes a few modules of library code specific to dugong and
trichechus. These modules include:

Cli: Handles the command line invocation of dugong and trichechus

Communication: Defines messages that are sent between dugong and trichechus
over the control connection to manage TEEs

## Libsirenia

The main library code for sirenia that is more general and useful for all parts
of Sirenia including [dugong], [trichechus], [manatee-runtime], and
[manatee-client]. These modules include:

**Communication:** General communication code that handles sending over a
connection and serialization and deserialization.

**Linux:** All Linux specific code that is necessary for Sirenia. This includes
events, which provides support for using EventMultiplexer, and syslog which
provides support for working with the syslog.

**Sandbox:** Support code for working with Minijail and sandboxing
applications. This is used to sandbox TEE apps.

**To_sys_util:** Abstraction of various low-level libc functionality.

**Transport:** Support code for transporting messages either over VSock or
IP between a server and a client.

## Manatee Runtime

Provides library functions for communicating with Trichechus from a TEE
application. More information can be found in the
[manatee-runtime README](./manatee-runtime/README.md).

## Manatee Client

Provides library functions for communicating with dugong over dbus. More
information can be found in the
[manatee-client README](./manatee-client/README.md)
(TODO: Add README for manatee-client).

[dugong]: #Dugong
[trichechus]: #Trichechus
[manatee-client]: #Manatee-Client
[manatee-runtime]: #Manatee-Runtime
