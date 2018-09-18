# Verity

Verity is the userspace tool for creating integrity hashes for a device image.

This tool is a frontend for dm-bht, a device-mapper friendly block hash
table structure.  `verity' produces dm-bht-based images for use with
dm-verity.  The dm-verity module provides a transparent, integrity-checking
layer over a given block device.  This expects a backing device and a secondary
device which provides cryptographic digests of the blocks on the primary
device

Note, the secondary device image can be appended to the primary device or
used as a standalone device.

This tool creates an image of the format:

* [hash of hash of blocks n ... n+n-1]
* [hash of hash of blocks 0 ... n-1]
* [...]
* [hash of block 1]
* [hash of block 0]

Upon completion, the hash of the root hash will be printed to standard
out.  The root hash, tree depth, number of hashed blocks, and cryptographic
hash algorithm used must be supplied to the dm-verity when configuring a
device.

## Building

To build outside of Chromium OS:
```sh
make
```

## Example Usage

To use:
```sh
./verity mode depth alg image hash_image [root_hexdigest]
```

For example:
```sh
dd if=/dev/zero of=/tmp/image bs=4k count=512
./verity create 2 sha256 /tmp/image /tmp/hash | tee table
# ...
cat table
ls -la /tmp/hash
```

## Licensing

All the source code is licensed GPLv2 to be completely kernel compatible.
The Makefiles are from the parent project and are licensed under a BSD-style
license.
