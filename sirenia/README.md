# Sirenia

The runtime environment and middleware for ManaTEE. It is named after the
taxonomic family manatees belong to. Trichechus runs as a daemon on the host
machine (or guest for TEE applications if it exists) while Dugong runs as a
daemon in the Chrome OS guest.

## Trichechus

The TEE application life-cycle manager. It is named after the genus manatees
belong to. Trichechus is a daemon that executes the TEE applications either
from a TEE application guest if it exists or on the host machine otherwise.

It serves the following purposes related to TEE applications:

1.  Loading and validation
2.  Instance launching and sandboxing
3.  Logging and crash dump collection
4.  Establishing communication
5.  Instance cleanup

## Dugong

The broker daemon implementation that communicates with Trichechus. It is named
after the cousin genus to manatees since this will run on the Chrome OS guest
machine, but will be closely related to Manatee and Trichechus.

Its roles include:

1.  Validating permissions and routing requests from untrusted applications
2.  Providing remote storage for Trichechus
    *   application binaries
    *   application data
    *   logs and crash dumps
3.  Routes log events and crash reports.
