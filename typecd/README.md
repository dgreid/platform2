# typecd: The Chrome OS USB Type C Daemon

## ABOUT

typecd is a system daemon for tracking the state of various USB Type C ports and connected
peripherals on a Chromebook. It interfaces with the Linux Type C connector class framework
to obtain notifications about Type C events, and to pull updated information about ports and
port-partners state.

## CLASS ORGANIZATION

The general structure of the classes is best illustrated by a few diagrams:

```
                         Daemon
                           |
                           |
                           |
        ------------------------------------------
        |                                         |
        |                                         |
        |                                         |
        |                                         |
   UdevMonitor    ---typec- udev- events--->   PortManager
```

### UdevMonitor

All communication and event notification between the Linux Kernel Type C connector class framework
and typecd occurs through the udev mechanism. This class watches for any events of the `typec` subsystem.
Other classes (in this case, `PortManager`) can register `Observer`s with `UdevMonitor`. When any notification
occurs, `UdevMonitor` calls the relevant function callback from the registered `Observer`s.

This class has basic parsing to determine which `Observer` should be called (is this a port/partner/cable notification?
is it add/remove/change?)

### PortManager

This class maintains a representation of all the state of a typec port exposed by the Type C connector class via sysfs.
The primary entity for `PortManager` is a Port.

```
           PortManager(UdevMonitor::Observer)
                           |
                           |
                           |
        ---------------------------------------
        |        |                            |
        |        |                            |
        |        |                            |
      Port0    Port1     ....               PortN
```

`PortManager` sub-classes `UdevMonitor::Observer`, and registers itself to receive typec event notifications. In turn, it
routes the notifications to the relevant object (Port, Partner, Cable) that the notification affects.

#### Port

This class represents a physical Type C connector on the host, along with components that are connected to it. Each `Port`
has a sysfs path associated with it of the form `/sys/class/typec/portX` where all the data (including relevant PD information)
is exposed by the kernel. On udev events this sysfs directory is read to update the `Port`'s in-memory state.
A `Port` can be detailed as follows:

```
                      Port
                       |
                       |
        -----------------------------------------------------------
        |                                 |                        |
        |                                 |                        |
  (sysfs path info)                    Partner                   Cable
```

##### Partner

This class represents a device/peripheral connected to a `Port`. There can only be 1 partner for each `Port`. Each `Partner` has
a sysfs path associated with it of the form `/sys/class/typec/portX-partner` where all the data (including relevant PD
information) is exposed by the kernel. On udev events this sysfs directory is read to update the `Partner`'s in-memory state.

This class also stores a list of Alternate Modes which are supported by the partner. Each Alternate mode is given an index according
to the index ascribed to it by the kernel.

```
                    Partner
                       |
                       |
        ------------------------------------------------------------------------
        |                        |                   |          |               |
        |                        |                   |          |               |
  (sysfs path info)      PD Identity info        AltMode0    AltMode1  ...   AltModeN
```

There are getters and setters to access the PD identity information (for example, `{Get,Set}ProductVDO()`).
There are also functions to retrieve information associated with partner altmodes, like getting a pointer to an altmode (`GetAltMode()`).

##### Cable

This class represents a cable that connects a `Port` to a `Partner`. There can only be 1 cable for each `Port`. Each `Cable` has
a sysfs path associated with it of the form `/sys/class/typec/portX-cable` where the PD identity data is exposed by the kernel.

This class also stores a list of Alternate Modes which are supported by the cable. Each Alternate mode is given an index according
to the index ascribed to it by the kernel. At present only SOP' cable plug alt modes are supported.
Even though each cable plug (i.e SOP' and SOP'') has its own device and sysfs path (of the form `/sys/class/typec/portX-plug.{0|1}`),
since the Chrome OS Embedded Controller (EC) only enumerates SOP' alt modes, we don't create a separate class and instead just list
the Alternate Modes of SOP' as belonging to the associated `Cable`.

When `UdevMonitor` receives an `add` event for a SOP' plug device, the `Cable` code searches through the corresponding sysfs file and adds all
the Alternate Modes associated with that file. We do this because the Type C connector class doesn't generate udev events for individual
SOP' cable plug alternate mode additions. TODO(b/174703000): Investigate why this is happening and fix it in the kernel.

```
                     Cable
                       |
                       |
        ------------------------------------------------------------------------------------
        |                        |                    |                |                    |
        |                        |                    |                |                    |
  (sysfs path info)      PD Identity info        SOP' AltMode0    SOP' AltMode1  ...   SOP' AltModeN
```

There are getters and setters to access the PD identity information (for example, `{Get,Set}ProductVDO()`).
There are also functions to retrieve information associated with partner altmodes, like getting a pointer to an altmode (`GetAltMode()`).
