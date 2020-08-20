# Diagnostic Routines

This guide details each of the diagnostic routines provided by cros_healthd,
along with any options the routine supports, a sample invocation run via the
diag component of cros-health-tool, and sample output from running the routine.
Routines can be run through crosh or directly through cros-health-tool. The
sample invocations below run the same routine for crosh and cros-health-tool.

[TOC]

## Routine Availability

Not all routines are available on all devices. For example, battery-related
routines are not available on Chromeboxes, which do not have batteries. To get a
list of all routines available on a given device, run the following command:

From crosh:
```bash
crosh> diag list
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=get_routines
```

Sample output:
```bash
Available routine: battery_capacity
Available routine: battery_health
...
Available routine: floating_point_accuracy
Available routine: prime_search
```

## Battery and Power Routines

### ac_power

Confirms that the AC power adapter is being recognized properly by the system.

Parameters:
-   `--ac_power_is_connected` - Whether or not a power supply is expected to be
    connected. Type: `bool`. Default: `true`.
-   `--expected_power_type` - The type of power supply expected to be connected.
    Only valid when `--ac_power_is_connected=true`. Type: `string`. Default:
    `""`

To ensure that a power supply of type USB_PD is connected and recognized:

From crosh:
```bash
crosh> diag ac_power --expected_power_type="USB_PD"
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=ac_power --expected_power_type="USB_PD"
```

Sample output:
```bash
Progress: 33
Plug in the AC adapter.
Press ENTER to continue.

Progress: 100
Status: Passed
Status message: AC Power routine passed.
```

### battery_capacity

Confirms that the device's battery design capacity lies within the given limits.

Parameters:
-   `--low_mah` - Lower bound for the allowable design capacity of the battery,
    in mAh. Type: `uint32_t`. Default: `1000`.
-   `--high_mah` - Upper bound for the allowable design capacity of the battery,
    in mAh. Type: `uint32_t`. Default: `10000`.

To ensure the device's battery capacity lies within the default range of
(1000, 10000) mAh:

From crosh:
```bash
crosh> diag battery_capacity
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=battery_capacity
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Battery design capacity within given limits.
```

### battery_charge

Checks to see if the battery charges appropriately during a period of time.

Parameters:
-   `--length_seconds` - Length of time to run the routine for, in seconds.
    Type: `uint32_t`. Default: `10`.
-   `--minimum_charge_percent_required` - Minimum charge percent required during
    the runtime of the routine. If, after the routine ends, the battery has
    charged less than this percent, then the routine fails. Type: `uint32_t`.
    Default: `0`.

To ensure the battery discharges less than 10 percent in 600 seconds:

From crosh:
```bash
crosh> diag battery_discharge --length_seconds=600 --maximum_discharge_percent_allowed=10
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=battery_discharge --length_seconds=600 --maximum_discharge_percent_allowed=10
```

Sample output:
```bash
Progress: 0
Unplug the AC adapter.
Press ENTER to continue.

Progress: 100
Output: Battery discharged 7% in 600 seconds.
Status: Passed
Status message: Battery discharge routine passed.
```

### battery_discharge

Checks to see if the battery discharges excessively during a period of time.

Parameters:
-   `--length_seconds` - Length of time to run the routine for, in seconds.
    Type: `uint32_t`. Default: `10`.
-   `--maximum_discharge_percent_allowed` - Maximum discharge percent allowed
    during the runtime of the routine. If, after the routine ends, the battery
    has discharged more than this percent, then the routine fails. Type:
    `uint32_t`. Default: `100`.

To ensure the battery discharges less than 10 percent in 600 seconds:

From crosh:
```bash
crosh> diag battery_discharge --length_seconds=600 --maximum_discharge_percent_allowed=10
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=battery_discharge --length_seconds=600 --maximum_discharge_percent_allowed=10
```

Sample output:
```bash
Progress: 0
Unplug the AC adapter.
Press ENTER to continue.

Progress: 100
Output: Battery discharged 7% in 600 seconds.
Status: Passed
Status message: Battery discharge routine passed.
```

### battery_health

Provides some basic information on the status of the battery, and determines if
the battery's cycle count and wear percentage are greater than the given limits.

Parameters:
-   `--maximum_cycle_count` - Upper bound for the battery's cycle count. Type:
    `uint32_t`. Default: `0`.
-   `--percent_battery_wear_allowed` - Upper bound for the battery's wear
    percentage. Type: `uint32_t`. Default: `100`.

To ensure the device's battery has a cycle count less than 5 and wear percentage
less than 15:

From crosh:
```bash
crosh> diag battery_health --maximum_cycle_count=5 --percent_battery_wear_allowed=15
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=battery_health --maximum_cycle_count=5 --percent_battery_wear_allowed=15
```

Sample output:
```bash
Progress: 100
Output: Charge Full: 4759000
Charge Full Design: 5275000
Charge Now: 4759000
Current Now: 0
Cycle Count: 10
Manufacturer: 333-22-
Present: 1
Status: Charging
Voltage Now: 13055000
Wear Percentage: 10

Status: Failed
Status message: Battery cycle count is too high.
```

## CPU Routines

### cpu_cache

Performs cache coherency testing via stressapptest --cc_test.

Parameters:
-   `--length_seconds` - Length of time to run the routine for, in seconds.
    Type: `uint32_t`. Default: `10`.

To run cache coherency testing for 600 seconds:

From crosh:
```bash
crosh> diag cpu_cache --length_seconds=600
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=cpu_cache --length_seconds=600
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Routine passed.
```

### cpu_stress

Performs CPU stress-testing via stressapptest -W, which mimics a realistic
high-load situation.

Parameters:
-   `--length_seconds` - Length of time to run the routine for, in seconds.
    Type: `uint32_t`. Default: `10`.

To run the stress test for the default 10 seconds:

From crosh:
```bash
crosh> diag cpu_stress
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=cpu_stress
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Routine passed.
```

### floating_point_accuracy

Repeatedly checks the accuracy of millions of floating-point operations against
known good values for the duration of the routine.

Parameters:
-   `--length_seconds` - Length of time to run the routine for, in seconds.
    Type: `uint32_t`. Default: `10`.

To perform floating-point operations for 300 seconds:

From crosh:
```bash
crosh> diag floating_point_accuracy --length_seconds=300
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=floating_point_accuracy --length_seconds=300
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Routine passed.
```

### prime_search

Repeatedly checks the CPU's brute-force calculations of prime numbers from 2 to
the given maximum number for the duration of the routine.

Parameters:
-   `--length_seconds` - Length of time to run the routine for, in seconds.
    Type: `uint32_t`. Default: `10`.
-   `--max_num` - Primes between two and this parameter will be calculated.
    Type: `uint64_t`. Default: `1000000`.

To search for prime numbers between 2 and 10000 for the default 10 seconds:

From crosh:
```bash
crosh> diag prime_search --max_num=10000
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=prime_search --max_num=10000
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Routine passed.
```

### urandom

Stresses the CPU by reading from /dev/urandom for the specified length of time.

Parameters:
-   `--length_seconds` - Length of time to run the routine for, in seconds.
    Type: `uint32_t`. Default: `10`.

To stress the CPU for 120 seconds:

From crosh:
```bash
crosh> diag urandom --length_seconds=120
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=urandom --length_seconds=120
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Routine passed.
```

## Memory Routines

### memory

Uses the memtester utility to run various subtests on the device's memory.

To run the memory routine:

From crosh:
```bash
crosh> diag memory
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=memory
```

Sample output:
```bash
Progress: 100
Output: memtester version 4.2.2 (64-bit)
Copyright (C) 2010 Charles Cazabon.

pagesize is 4096
pagesizemask is 0xfffffffffffff000
want 100MB (104857600 bytes)
got  100MB (104857600 bytes), trying mlock ...locked.
Loop 1/1:
  Stuck Address       : ok
  Random Value        : ok
  Compare XOR         : ok
  Compare SUB         : ok
  Compare MUL         : ok
  Compare DIV         : ok
  Compare OR          : ok
  Compare AND         : ok
  Sequential Increment: ok
  Solid Bits          : ok
  Block Sequential    : ok
  Checkerboard        : ok
  Bit Spread          : ok
  Bit Flip            : ok
  Walking Ones        : ok
  Walking Zeroes      : ok

Done.

Status: Passed
Status message: Memory routine passed.
```

## Storage Routines

### disk_read

Uses the fio utility to write a temporary file with random data, then repeatedly
read the file either randomly or linearly for the duration of the routine.
Checks to see that the data read matches the data written.

Parameters:
-   `--length_seconds` - Length of time to run the routine for, in seconds.
    Type: `uint32_t`. Default: `10`.
-   `--disk_read_routine_type` - Type of reading to perform. Type: `string`.
    Default: `linear`. Allowable values: `[linear|random]`
-   `--file_size_mb` - Size of the file to read and write, in MB. Type:
    `int32_t`. Default: `1024`.

To read a test file of size 10MB randomly for 120 seconds:

From crosh:
```bash
crosh> diag disk_read --length_seconds=120 --disk_read_routine_type="random" --file_size_mb=10
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=disk_read --length_seconds=120 --disk_read_routine_type="random" --file_size_mb=10
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Routine passed.
```

### nvme_self_test

Conducts either a short or a long self-test of the device's NVMe storage.

Parameters:
-   `--nvme_self_test_long` - Whether or not to conduct a long self-test. Type:
    `bool`. Default: `false`.

To conduct a short self-test of the device's NVMe storage:

From crosh:
```bash
crosh> diag nvme_self_test
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=nvme_self_test
```

Sample output:
```bash
Progress: 100
Output: AQAAABAAAAA7AAAAAAAAAA==
Status: Passed
Status message: SelfTest status: Test PASS
```

### nvme_wear_level

Compares the device's NVMe storage's wear level against the input threshold.

Parameters:
-   `--wear_level_threshold` - Acceptable wear level for the device's NVMe
    storage. Type: `uint32_t`. Default: `50`. Allowable values: `(0,99)`

To ensure the device's NVMe storage has a wear level no more than 20:

From crosh:
```bash
crosh> diag nvme_wear_level --wear_level_threshold=20
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=nvme_wear_level --wear_level_threshold=20
```

Sample output:
```bash
Progress: 100
Output: AAAAAAAAAADxBAAAAAAAAA==
Status: Passed
Status message: Wear-level status: PASS.
```

### smartctl_check

Checks to see if the drive's remaining spare capacity is high enough to protect
against asynchronous event completion.

The smartctl_check routine has no parameters.

To check that the device's spare capacity is sufficient:

From crosh:
```bash
crosh> diag smartctl_check
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=smartctl_check
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Routine passed
```

## Network Routines

### lan_connectivity

Checks to see whether the device is connected to a LAN.

The lan_connectivity routine has no parameters.

To check whether a device is connected to a LAN:

From crosh:
```bash
crosh> diag lan_connectivity
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=lan_connectivity
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Lan Connectivity routine passed with no problems.
```

### signal_strength

Checks to see whether there is an acceptable signal strength on wireless
networks.

The signal_strength routine has no parameters.

To check whether there is an acceptable signal strength on wireless networks:

From crosh:
```bash
crosh> diag signal_strength
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=signal_strength
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Signal Strength routine passed with no problems.
```
