# Shill Architecture

## Devices

In Shill, a network interface is represented by a `Device` instance. Among other
things, the `Device` class provides basic functionality to configure its
interface through `/proc/sys/net` parameters, to acquire and use IP
configuration parameters, and to drive [`Service`](#services) state. `Device` is
the base class for a hierarchy of `Device` children that perform
technology-specific behavior and that actually trigger base `Device`
functionality:

![Device Inheritance](images/device_inheritance.png)

Most `Device` instances are created by the `DeviceInfo` class. `DeviceInfo`
listens to device and address-configuration events using [`RTNL`](#glossary). On
startup, it also requests a dump of existing device and address-configuration
information in order to be in sync with the current system state. When
`DeviceInfo` has enough information about an interface, it will create a
`Device` child instance of the proper type. `DeviceInfo` will also update
existing `Device` instances with new information that it receives about the
corresponding interface. A common exception to the "`Devices` are created by
`DeviceInfo`" rule are `VirtualDevices` corresponding to virtual interfaces
created by Shill (for use-cases like PPPoE, VPN, and cellular
dongles). `Cellular` instances are another exception, which are created by
`Modem` instances (which are themselves a representation of the ModemManager
Modem D-Bus object). For the cellular case, `DeviceInfo` sends relevant link
information to the `ModemInfo` class.

## Services

A network interface on its own doesn't magically provide network connectivity,
but instead must connect to "something" (e.g., a WiFi Access Point) in order to
achieve that end result. A `Service` represents that "something"; it is a
representation of an entity that can provide network connectivity if interacted
with properly. The `Service` class acts as a connection state machine that
`Device` piggybacks on, provides the core connect/disconnect behavior, and
provides the basic functionality for persisting network-specific configuration
options, among other things. Not unlike `Device`, `Service` also forms a
hierarchy in which children perform technology-specific behavior:

![Service Inheritance](images/service_inheritance.png)

A `Service` and `Device` of matching technology type must interact (conceptually
they are bound to each other) in order for the `Service` to reach a connected
state.

`Service` instances in most cases are created by `Provider` instances. At most
one instance exists for each child `Provider` type. `Provider` instances
generally create new `Services` either from persisted state (see the
[`Profile`](#profiles) section) or from the D-Bus interface (see the
[`Manager`](#manager) section).
For `WiFiProvider`, `Services` are also created based on network scans performed
by `wpa_supplicant`, which leads to the reception of BSSAdded D-Bus signals that
trigger `WiFiProvider` to create a corresponding `WiFiService`.
The `CellularServiceProvider` instance ensures that a `CellularService` for a
`Cellular` Device is created when it is ready, corresponding to the IMSI
associated with the active SIM. Additional `CellularService` instances may also
be created if they match the SIM Card Id (eID or ICCID) of the active SIM.
The `EthernetProvider` by default has a single `EthernetService`, which the
first `Ethernet` instance will use. Additional `Ethernet` instances will cause
the `EthernetProvider` to create additional `EthernetService` instances.

![Provider Inheritance](images/provider_inheritance.png)

## Profiles

A `Profile` represents a set of persisted data and is used for both `Device` and
`Service`. `Device` and `Service` (base classes and children) take care of
loading from and saving to the underlying storage used by the relevant
`Profile`.

![Profile Inheritance](images/profile_inheritance.png)

Shill allows for a stack of `Profiles` rather than just having a single
`Profile` at a time. On startup, the `Profile` stack contains a single
`DefaultProfile`, which serves to provide pre-login configuration data. In
addition to the regular `Profile` behavior of persisting `Service` properties, a
`DefaultProfile` will also persist `Device` properties and a subset of `Manager`
properties.

On user login, the [shill_login_user](../bin/shill_login_user) script is run,
which creates a `Profile` for that user if one doesn't already exist, and then
pushes that `Profile` onto the `Profile` stack. Correspondingly,
[shill_logout_user](../bin/shill_logout_user) will pop the user's `Profile` when
logging out. When a guest user is used, a `Profile` will still be created and
pushed onto the stack as usual, but the persisted data will simply be deleted so
that the data is essentially not persisted. An `EphemeralProfile`, which is
essentially a "null profile" that doesn't persist any data, is *not* used for
guest users. One benefit of this is resilience to Shill crashes within a single
guest user session.

Every `Service` has exactly one `Profile` associated with it, which is
the`Profile` most recently pushed onto the `Profile` stack that contains
persisted data for that `Service`. An `EphemeralProfile` is used exclusively for
`Service` instances that are created but that have no `Profile` in the `Profile`
stack that contains data for it (e.g., a `WiFiService` that was created from a
WiFi scan but that the user has never attempted to configure or connect to). A
`Service` can be "linked" to a different `Profile` through the use of the
`Service` kProfileProperty D-Bus property, which is how `Service` instances
currently using the `EphemeralProfile` can be moved over to a `Profile` that
allows its configuration parameters to be persisted.

Given that Shill's D-Bus clients aside from Autotest/Tast do not create
additional `Profiles`, the `Profile` stack in non-test cases contains only the
`DefaultProfile` and potentially a user `Profile`. The `EphemeralProfile` is not
part of the `Profile` stack.

## Manager

`Manager` is the "top-level" class in Shill. Its responsibilities include:
*   Keeping track of `Devices` representing interfaces managed by Shill.
    *   `Manager` is informed of `Device` additions/removals by `DeviceInfo`.
*   Keeping track of `Service` instances.
*   Maintaining the `Profile` stack, which includes updating `Device` and
    `Service` instances when `Profiles` are pushed onto or popped from the
    stack.
*   Keeping `Service` instances sorted according to the Service ordering
    described in the [`Manager` D-Bus Specification].
*   Initiating `Service` auto-connections (a `Service` determines whether or
    not it can auto-connect, but `Manager` triggers the auto-connection
    attempt).
*   Interfacing with the PowerManager daemon to trigger pre-suspend or
    post-resume behavior in `Devices` and `Services`.
*   Storing top-level properties that are set from the command-line.

`Manager` also provides the top-level Shill D-Bus interface. Note that `Device`,
`Service`, and `Profile` instances tracked by `Manager` will all have
corresponding D-Bus objects that clients can interact with. Clients first learn
about these objects via the `Manager` D-Bus object.

# Glossary

*   **`RTNL`** - Acronym for [rtnetlink].

    [Netlink] is a protocol that can be used for kernel <-> user-space and
    user-space <-> user-space communication. Unlike a syscall, which requires a
    user to initiate communication with the kernel, any entity may send netlink
    messages at any time. This allows the kernel to notify listeners of events
    without those listeners needing to do something like poll for a state
    change. It is based on the socket API, and therefore provides built-in
    asynchronous semantics through the use of socket queues. Netlink also
    provides support for multicast, which allows--for example--multiple
    user-space daemons to listen to the same events from the kernel simply by
    registering with the appropriate multicast group(s).

    Different usages of the netlink protocol will have different information
    being sent and will generally want multicast groups isolated from other
    netlink usages. Netlink employs the concept of a **family** to satisfy this
    need, where a family can define custom multicast groups and message
    types. rtnetlink refers specifically to the NETLINK_ROUTE netlink family,
    which is defined by the kernel itself (see [rtnetlink.h]).


[`Manager` D-Bus Specification]: manager-api.txt
[Netlink]: http://man7.org/linux/man-pages/man7/netlink.7.html
[rtnetlink]: http://man7.org/linux/man-pages/man7/rtnetlink.7.html
[rtnetlink.h]: https://elixir.bootlin.com/linux/v5.0/source/include/uapi/linux/rtnetlink.h
