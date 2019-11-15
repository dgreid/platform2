# Fingerprint Study Tool

<!-- mdformat off(Gitiles note syntax will get mangled by mdformat) -->
*** note
See [Typography conventions] to understand what `(outside)`, `(inside)`,
`(in/out)`, and `(device)` mean.
***
<!-- mdformat on -->

## Running Fingerprint Study

1.  On the host, run the following commands:

    ```bash
    (inside) $ BOARD=hatch
    (inside) $ DUT=dut1
    (inside) $ emerge-$BOARD fingerprint_study
    (inside) $ cros deploy $DUT fingerprint_study
    ```

2.  Reboot the device.

3.  Navigate to http://localhost:9000 in a web browser.

## Test on Host Using Mock ectool

We will use a python virtual environment to ensure proper dependency versions
and a mock `ectool` in [mock-bin](mock-bin). Note, the mock ectool will
effectively emulate an immediate finger press when the study tool requests a
finger press. This does not make use of the FPC python library.

1.  Run the following command:

    ```bash
    (in/out) $ ./host-run.sh
    ```

2.  Finally, navigate to http://127.0.0.1:9000 in a web browser.

## Running Fingerprint Study Using Python Virtualenv

This is an **alternative method** to install the fingerprint study tool on a
test device. It bypasses the Chrome OS/Gentoo dependencies and allows using
providing a clean virtualenv for the execution on the test device.

### 1) Build python3 virtual environment bundle

```bash
# Optionally, you can build the virtual environment in a Docker container.
# docker run -v$HOME/Downloads:/Downloads -it debian
# On Debian, ensure that git, python3, python3-pip, and virtualenv are installed.
(outside) $ sudo apt update && apt install git python3 python3-pip virtualenv
# Grab the fingerprint study tool source
(outside) $ git clone https://chromium.googlesource.com/chromiumos/platform2
# Create an isolated python3 environment
(outside) $ virtualenv -p python3 /tmp/fpstudy-virtualenv
(outside) $ . /tmp/fpstudy-virtualenv/bin/activate
# Install fingerprint study dependencies
(outside) $ pip3 install -r platform2/biod/study/requirements.txt
# Copy the fingerprint study source
(outside) $ cp -r platform2/biod/study /tmp/fpstudy-virtualenv
# Bundle the virtual environment with study source
(outside) $ tar -C /tmp -czvf /tmp/fpstudy-virtualenv.tar.gz fpstudy-virtualenv
# For Docker with Downloads volume shared, run the following command:
# cp /tmp/fpstudy-virtualenv.tar.gz /Downloads/
```

The output of these steps is the `fpstudy-virtualenv.tar.gz` archive.

### 2) Enable developer mode on the chromebook

See [Enable Developer Mode].

### 3) Install python3 virtual environment bundle

Transfer the `fpstudy-virtualenv.tar.gz` bundle to the test device.

One such method is to use scp, like in the following command:

```bash
(in/out) $ scp fpstudy-virtualenv.tar.gz root@$DUTIP:/root/
```

On the test device, extract the bundle into `/opt/google`, as shown in the
following command set:

```bash
(device) $ mkdir -p /opt/google
(device) $ tar -xzvf /root/fpstudy-virtualenv.tar.gz -C /opt/google
```

Enable the fingerprint study Upstart job.

```bash
(device) $ ln -s /opt/google/fpstudy-virtualenv/study/fingerprint_study_virtualenv.conf /etc/init
(device) $ start fingerprint_study_virtualenv
(device) $ sleep 2
(device) $ status fingerprint_study_virtualenv
```

### 4) Configure

To configure the number of fingers, enrollment taps, and verification taps
expected by the fingerprint study tool, please modify
`/opt/google/fpstudy-virtualenv/study/fingerprint_study_virtualenv.conf`.

### 5) Test

Navigate to http://127.0.0.1:9000 in a web browser.

### Build Fingerprint Study Image

This is an **untested** method of building an entire Chrome OS image with the
fingerprint_study package included.

```bash
(inside) $ BOARD=hatch
(inside) $ USE=fpstudy ./build_packages --board=$BOARD
(inside) $ ./build_image --board=$BOARD
(inside) $ cros flash usb:// $BOARD/latest
```

[Enable Developer Mode]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_mode.md#dev-mode
[Typography conventions]: https://chromium.googlesource.com/chromiumos/docs/+/HEAD/developer_guide.md#typography-conventions
