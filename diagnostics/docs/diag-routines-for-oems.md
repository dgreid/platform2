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

## Routine Configuration

Some routines use configuration data read from cros_config instead of exposing
parameters in the Mojo API. Configuration data is device-specific. If a board
runs a configurable routine whose configuration data is not set in cros_config,
the routine will fall back to fleet-wide defaults. Configuration data will be
listed in the description of routines which support it. In all cases, the data
should be set the cros_config path cros-healthd/routines/specific-routine. For a
concrete example, the battery health configuration data would look like the
following:

```yaml
some-config: &some_config
  <<: *base_config
  cros-healthd:
    routines:
      battery-health:
        maximum-cycle-count: "5"
        percent-battery-wear-allowed: "15"
```

## Battery and Power Routines

### AC Power

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

### Battery Capacity

Confirms that the device's battery design capacity lies within the configured
limits.

Configuration Data::
-   `low-mah` - Lower bound for the allowable design capacity of the battery, in
    mAh. Type: `uint32_t`. Default: `1000`.
-   `high-mah` - Upper bound for the allowable design capacity of the battery,
    in mAh. Type: `uint32_t`. Default: `10000`.

To check the device's battery capacity:

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

### Battery Charge

Checks to see if the battery charges appropriately during a period of time.

Parameters:
-   `--length_seconds` - Length of time to run the routine for, in seconds.
    Type: `uint32_t`. Default: `10`.
-   `--minimum_charge_percent_required` - Minimum charge percent required during
    the runtime of the routine. If, after the routine ends, the battery has
    charged less than this percent, then the routine fails. Type: `uint32_t`.
    Default: `0`.

The battery charge routine will return an error if the sum of
`--minimum_charge_percent_required` and the charge percentage of the device's
battery when the routine is started is greater than 100%. For example, if the
device's battery is at 90% and the following command was run from crosh:
```bash
crosh> diag battery_charge --minimum_charge_percent_required=20
```

Then the output would be:
```bash
Progress: 0
Unplug the AC adapter.
Press ENTER to continue.

Progress: 0
Output: {
    "errorDetails": {
        "chargePercentRequested": 20,
        "startingBatteryChargePercent": 90
    }
}

Status: Error
Status message: Invalid minimum required charge percent requested.
```

Assuming the device's battery is less than 91%, then to ensure the battery
charges at least than 10 percent in 600 seconds:

From crosh:
```bash
crosh> diag battery_charge --length_seconds=600 --minimum_charge_percent_required=10
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=battery_charge --length_seconds=600 --minimum_charge_percent_required=10
```

Sample output, if the battery were to charge 12.123456789012345% during the routine:
```bash
Progress: 0
Unplug the AC adapter.
Press ENTER to continue.

Progress: 100
Output: {
    "resultDetails": {
        "chargePercent": 12.123456789012345
    }
}

Status: Passed
Status message: Battery charge routine passed.
```

### Battery Discharge

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

Sample output, if the battery were to discharge 1.123456789012345% during the routine:
```bash
Progress: 0
Unplug the AC adapter.
Press ENTER to continue.

Progress: 100
Progress: 100
Output: {
    "resultDetails": {
        "dischargePercent": 1.123456789012345
    }
}

Status: Passed
Status message: Battery discharge routine passed.
```

### Battery Health

Provides some basic information on the status of the battery, and determines if
the battery's cycle count and wear percentage are greater than the given limits.

Configuration Data:
-   `maximum-cycle-count` - Upper bound for the battery's cycle count. Type:
    `uint32_t`. Default: `1000`.
-   `percent-battery-wear-allowed` - Upper bound for the battery's wear
    percentage. Type: `uint32_t`. Default: `50`.

To run the battery health routine:

From crosh:
```bash
crosh> diag battery_health
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=battery_health
```

Sample output:
```bash
Progress: 100
Output: {
    "resultDetails": {
        "chargeFull": 4621000,
        "chargeFullDesign": 5275000,
        "chargeNow": 4621000,
        "currentNow": 0,
        "cycleCount": 20,
        "manufacturer": "333-22-",
        "present": 1,
        "status": "Charging",
        "voltageNow": 13023000,
        "wearPercentage": 13
    }
}

Status: Passed
Status message: Routine passed.
```

## CPU Routines

### CPU Cache

Performs cache coherency testing via stressapptest --cc_test.

Parameters:
-   `--cpu_stress_length_seconds` - Length of time to run the routine for, in
    seconds. Type: `uint32_t`. Default: `60`.

To run cache coherency testing for 600 seconds:

From crosh:
```bash
crosh> diag cpu_cache --cpu_stress_length_seconds=600
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=cpu_cache --cpu_stress_length_seconds=600
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Routine passed.
```

### CPU stress

Performs CPU stress-testing via stressapptest -W, which mimics a realistic
high-load situation.

Parameters:
-   `--cpu_stress_length_seconds` - Length of time to run the routine for, in
    seconds. Type: `uint32_t`. Default: `60`.

To run the stress test for the default 60 seconds:

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

### Floating Point Accuracy

Repeatedly checks the accuracy of millions of floating-point operations against
known good values for the duration of the routine.

Parameters:
-   `--cpu_stress_length_seconds` - Length of time to run the routine for, in
    seconds. Type: `uint32_t`. Default: `60`.

To perform floating-point operations for 300 seconds:

From crosh:
```bash
crosh> diag floating_point_accuracy --cpu_stress_length_seconds=300
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=floating_point_accuracy --cpu_stress_length_seconds=300
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Routine passed.
```

### Prime Search

Repeatedly checks the CPU's brute-force calculations of prime numbers from 2 to
the given maximum number for the duration of the routine.

Configuration Data:
-   `max-num` - Primes between two and this parameter will be calculated. Type:
    `uint64_t`. Default: `1000000`.

Parameters:
-   `--cpu_stress_length_seconds` - Length of time to run the routine for, in
    seconds. Type: `uint32_t`. Default: `60`.

To search for prime numbers for the default 60 seconds:

From crosh:
```bash
crosh> diag prime_search
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=prime_search
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Routine passed.
```

### Urandom

Stresses the CPU by reading from /dev/urandom for the specified length of time.

Parameters:
-   `--urandom_length_seconds` - Length of time to run the routine for, in
    seconds. Type: `uint32_t`. Default: `10`.

To stress the CPU for 120 seconds:

From crosh:
```bash
crosh> diag urandom --urandom_length_seconds=120
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=urandom --urandom_length_seconds=120
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Routine passed.
```

## Memory Routines

### Memory

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
Output: {
   "resultDetails": {
      "bytesTested": 104857600,
      "memtesterVersion": "4.2.2 (64-bit)",
      "subtests": {
         "bitFlip": "ok",
         "bitSpread": "ok",
         "blockSequential": "ok",
         "checkerboard": "ok",
         "compareAND": "ok",
         "compareDIV": "ok",
         "compareMUL": "ok",
         "compareOR": "ok",
         "compareSUB": "ok",
         "compareXOR": "ok",
         "randomValue": "ok",
         "sequentialIncrement": "ok",
         "solidBits": "ok",
         "stuckAddress": "ok",
         "walkingOnes": "ok",
         "walkingZeroes": "ok"
      }
   }
}

Status: Passed
Status message: Memory routine passed.
```

## Storage Routines

### Disk Read

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

### NVMe Self Test

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
Output: {
    "resultDetails": {
        "rawData": "AQAAABAAAAA7AAAAAAAAAA=="
    }
}

Status: Passed
Status message: SelfTest status: Test PASS
```

### NVMe Wear Level

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
Output: {
    "resultDetails": {
        "rawData": "AAAAAAAAAADxBAAAAAAAAA=="
    }
}

Status: Passed
Status message: Wear-level status: PASS.
```

### Smartctl Check

Checks to see if the drive's remaining spare capacity is high enough to protect
against asynchronous event completion.

The smartctl check routine has no parameters.

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

### LAN Connectivity

Checks to see whether the device is connected to a LAN.

The LAN connectivity routine has no parameters.

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

### Signal Strength

Checks to see whether there is an acceptable signal strength on wireless
networks.

The signal strength routine has no parameters.

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

### Gateway can be Pinged

Checks whether the gateway of connected networks is pingable.

The gateway can be pinged routine has no parameters.

To check whether the gateway of connected networks is pingable:

From crosh:
```bash
crosh> diag gateway_can_be_pinged
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=gateway_can_be_pinged
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Gateway Can Be Pinged routine passed with no problems.
```

### Has Secure WiFi Connection

Checks whether the WiFi connection is secure. Note that if WiFi is not
connected, the routine will not run.

The has secure WiFi connection routine has no parameters.

To check whether the WiFi connection is secure:

From crosh:
```bash
crosh> diag has_secure_wifi_connection
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine
--routine=has_secure_wifi_connection
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Has Secure WiFi Connection routine passed with no problems.
```

### DNS Resolver Present

Checks whether a DNS resolver is available to the browser.

The DNS resolver present routine has no parameters.

To run the DNS resolver present routine:

From crosh:
```bash
crosh> diag dns_resolver_present
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=dns_resolver_present
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: DNS resolver present routine passed with no problems.
```

### DNS Latency

Checks whether the DNS latency is below an acceptable threshold.

The DNS latency routine has no parameters.

To run the DNS latency routine:

From crosh:
```bash
crosh> diag dns_latency
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=dns_latency
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: DNS latency routine passed with no problems.
```

### DNS Resolution

Checks whether a DNS resolution can be completed successfully.

The DNS resolution routine has no parameters.

To run the DNS resolution routine:

From crosh:
```bash
crosh> diag dns_resolution
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine
--routine=dns_resolution
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: DNS resolution routine passed with no problems.
```

### Captive Portal

Checks whether the internet connection is behind a captive portal.

The captive portal routine has no parameters.

To run the captive portal routine:

From crosh:
```bash
crosh> diag captive_portal
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine
--routine=captive_portal
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: Captive portal routine passed with no problems.
```

### HTTP Firewall

Checks whether a firewall is blocking HTTP port 80.

The HTTP firewall routine has no parameters.

To run the HTTP firewall routine:

From crosh:
```bash
crosh> diag http_firewall
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine
--routine=http_firewall
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: HTTP firewall routine passed with no problems.
```

### HTTPS Firewall

Checks whether a firewall is blocking HTTPS port 443.

The HTTPS firewall routine has no parameters.

To run the HTTPS firewall routine:

From crosh:
```bash
crosh> diag https_firewall
```

From cros-health-tool:
```bash
$ cros-health-tool diag --action=run_routine --routine=https_firewall
```

Sample output:
```bash
Progress: 100
Status: Passed
Status message: HTTPS firewall routine passed with no problems.
```
