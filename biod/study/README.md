# Running Fingerprint Study

## Install

```bash
BOARD=nocturne
DUT=dut1

emerge-$BOARD fingerprint_study
cros deploy $DUT fingerprint_study
```

## Launch

```bash
# On device
start fingerprint_study
# Navigate to http://localhost:9000
```

## Test on host

We will use a python virtual environment to ensure proper dependency versions
and a mock `ectool` in [mock-bin](mock-bin). Note, the mock ectool will
effectively emulate an immediate finger press when the study tool requests a
finger press. This does not make use of the FPC python library.

```bash
rm -rf /tmp/virtualenv-study
virtualenv -p python3 /tmp/virtualenv-study
. /tmp/virtualenv-study/bin/activate
pip3 install -r requirements.txt
PATH=$(pwd)/mock-bin:$PATH ./study_serve.py
```

Finally, navigate to http://127.0.0.1:9000 in a web browser.

# Directories

## mock-bin

This directory is intended to be added to the `PATH` while testing the
fingerprint study tool on a host platform (non-chromebook machine).

# Setup Using Python Virtualenv

## 1) Build python3 virtual environment bundle

```bash
# Optionally, you can build the virtual environment in a Docker container.
# docker run -v$HOME/Downloads:/Downloads -it debian
# On Debian, ensure that git, python3, python3-pip, and virtualenv are installed.
apt update && apt install git python3 python3-pip virtualenv
# Grab the fingerprint study tool source
git clone https://chromium.googlesource.com/chromiumos/platform2
# Create an isolated python3 environment
virtualenv -p python3 /tmp/fpstudy-virtualenv
. /tmp/fpstudy-virtualenv/bin/activate
# Install fingerprint study dependencies
pip3 install -r platform2/biod/study/requirements.txt
# Copy the fingerprint study source
cp -r platform2/biod/study /tmp/fpstudy-virtualenv
# Bundle the virtual environment with study source
tar -C /tmp -czvf /tmp/fpstudy-virtualenv.tar.gz fpstudy-virtualenv
# For Docker with Downloads volume shared, run the following command:
# cp /tmp/fpstudy-virtualenv.tar.gz /Downloads/
```
The output of these steps is the `fpstudy-virtualenv.tar.gz` archive.

## 2) Enable developer mode on the chromebook
See https://chromium.googlesource.com/chromiumos/docs/+/master/developer_mode.md.

## 3) Install python3 virtual environment bundle

Transfer the `fpstudy-virtualenv.tar.gz` bundle to the test device.

One such method is to use scp, like in the following command:
```bash
scp fpstudy-virtualenv.tar.gz root@$DUTIP:/root/
```

On the test device, extract the bundle into `/opt/google`, as shown in the
following command set:
```bash
mkdir -p /opt/google
tar -xzvf /root/fpstudy-virtualenv.tar.gz -C /opt/google
```
Enable the fingerprint study Upstart job.
```bash
ln -s /opt/google/fpstudy-virtualenv/study/fingerprint_study_virtualenv.conf /etc/init
start fingerprint_study_virtualenv
sleep 2
status fingerprint_study_virtualenv
```

## 4) Configure

To configure the number of fingers, enrollment taps, and verification taps
expected by the fingerprint study tool, please modify
`/opt/google/fpstudy-virtualenv/study/fingerprint_study_virtualenv.conf`.

## 5) Test

Navigate to http://127.0.0.1:9000 in a web browser.
