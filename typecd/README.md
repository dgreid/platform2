# typecd: The Chrome OS USB Type C Daemon

## ABOUT

typecd is a system daemon for tracking the state of various USB Type C ports and connected
peripherals on a Chromebook. It interfaces with the Linux Type C connector class framework
to obtain notifications about Type C events, and to pull updated information about port and
port-partner state.

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
occurs, `UdevMonitor` calls the relevant function callback from the registered observers.

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
        ----------------------------------
        |                                 |
        |                                 |
  (sysfs path info)                    Partner
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
