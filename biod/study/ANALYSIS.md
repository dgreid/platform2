# Fingerprint Performance Analysis

This document gives a high level overview of what is required for the post
fingerprint sample collection analysis.

## Vendor Requirements

If the FPMCU fingerprint matching algorithm is provided by the vendor, we need
an equivalent matching performance analysis tool that can be run on on the host
side. Although this tool must be capable of running on a GNU/Linux machine, this
tool must accurately measure the performance of the provided FPMCU matching
algorithm.

## Process

1.  Capture participant fingerprint samples using the
    [Fingerprint Study Tool](README.md).
2.  Run the analysis tool on the captured fingerprint samples to determine if
    the fingerprint matching performance meets Chrome OS Fingerprint standards.
