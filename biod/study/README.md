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
# Disable biod upstart
stop biod
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
