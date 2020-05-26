# Chrome OS Iio Service

The repository hosts the core Chrome OS platform iioservice components, including:

- Mojo IPC library for clients to connect to iioservice

## Mojo IPC library

This library provides mojo interfaces to connect to sensor_dispatcher and
iioservice, and C++ utility functions and classes to establish mojo channels.

## Daemon iioservice

- `/usr/sbin/iioservice`

This daemon provides mojo channels that let processes connect to it. iioservice
will dispatch devices' event data to the processes that need them. Each process
can set the desired frequencies and IIO channels of devices without conflicts,
as it owns all IIO devices.
