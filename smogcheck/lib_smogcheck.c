// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// lib_smogcheck.c: C code to make direct I2C calls using ioctl().
//
// Adapted and modified from:
//   http://www.mjmwired.net/kernel/Documentation/i2c/dev-interface

#include "lib/lib_smogcheck.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define arraysize(x) (sizeof(x) / sizeof(x[0]))

// Opens a device file for I/O operations.
//
// Args:
//   adapter_nr: an integer, adapater's number address.
//
// Returns:
//   a file descriptor (non-negative integer), ready to use. Or -1 if error.
int GetDeviceFile(int adapter_nr) {
  int file;
  char filename[20] = {0};

  snprintf(filename, arraysize(filename) - 1, "/dev/i2c-%d", adapter_nr);
  printf("Attempt to open device %s\n", filename);
  file = open(filename, O_RDWR);
  if (file < 0) {
    printf("Error opening file %d: %s\n", errno, strerror(errno));
    return -1;
  }
  printf("Successfully opened device %s\n", filename);
  return file;
}

// Sets I2C device address to communicate with.
//
// Args:
//   fd: an integer, open device file descriptor.
//   addr: an integer, I2C slave address to set.
//
// Returns:
//   0 if success, -1 if error.
int SetSlaveAddress(int fd, int addr) {
  if (fd < 0) {
    printf("Error: invalid file descriptor %d. Expect integer >= 0\n", fd);
    return -1;
  } else if (addr < 0x08 || addr > 0x77) {
    printf(
        "Error: invalid 7-bit I2C slave address %d. Expect range is "
        "an integer in [8, 112]\n",
        addr);
    return -1;
  }

  if (ioctl(fd, I2C_SLAVE, addr) < 0) {
    printf("Error communicating to slave address: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

// Precondition checks.
//
// Args:
//   fd: a non-negative integer, open device file descriptor.
//
// Returns:
//   ret: a boolean, true if fd is non-negative and false otherwise.
bool PreconditionCheck(int fd) {
  if (fd < 0) {
    printf("Error: invalid file descriptor %d. Expect integer >= 0\n", fd);
    return false;
  }
  return true;
}

// Writes a byte to specified register address.
//
// Args:
//   fd: an integer, open device file descriptor.
//   reg: an 8-bit unsigned integer, non-negative register number.
//   byte_val: an 8-bit unsigned integer, value to write.
//
// Returns:
//   0 if success, -1 if error.
int WriteByte(int fd, uint8_t reg, uint8_t byte_val) {
  union i2c_smbus_data data;
  data.byte = byte_val;
  struct i2c_smbus_ioctl_data args;
  args.read_write = I2C_SMBUS_WRITE;
  args.command = reg;
  args.size = I2C_SMBUS_BYTE_DATA;
  args.data = &data;

  if (PreconditionCheck(fd) != true) {
    return -1;
  }

  if (ioctl(fd, I2C_SMBUS, &args)) {
    printf("Error writing to register: %s\n", strerror(errno));
    return -1;
  }
  printf("Wrote to register %d: 0x%x\n", reg, byte_val);
  return 0;
}

// Reads a byte value from a specified register address.
//
// Args:
//   fd: an integer, open device file descriptor.
//   reg: an 8-bit unsigned integer, non-negative register number.
//
// Returns:
//   byte value read if success, -1 if error.
int ReadByte(int fd, uint8_t reg) {
  union i2c_smbus_data data;
  struct i2c_smbus_ioctl_data args;
  args.read_write = I2C_SMBUS_READ;
  args.command = reg;
  args.size = I2C_SMBUS_BYTE_DATA;
  args.data = &data;

  if (PreconditionCheck(fd) != true) {
    return -1;
  }

  if (ioctl(fd, I2C_SMBUS, &args)) {
    printf("Error reading from bus: %s\n", strerror(errno));
    return -1;
  }
  printf("Read back from bus: 0x%x\n", data.byte);
  return data.byte;
}

// Writes a word to specified register address.
//
// Args:
//   fd: an integer, open device file descriptor.
//   reg: an 8-bit unsigned integer, register number.
//   word_val: a 16-bit unsigned integer, value to write.
//
// Returns:
//   0 if success, -1 if error.
int WriteWord(int fd, uint8_t reg, uint16_t word_val) {
  union i2c_smbus_data data;
  data.word = word_val;
  struct i2c_smbus_ioctl_data args;
  args.read_write = I2C_SMBUS_WRITE;
  args.command = reg;
  args.size = I2C_SMBUS_WORD_DATA;
  args.data = &data;

  if (PreconditionCheck(fd) != true) {
    return -1;
  }

  if (ioctl(fd, I2C_SMBUS, &args)) {
    printf("Error writing to register: %s\n", strerror(errno));
    return -1;
  }
  printf("Wrote to register %d: 0x%x\n", reg, word_val);
  return 0;
}

// Reads a word from a specified register address.
//
// Args:
//   fd: an integer, open device file descriptor.
//   reg: an 8-bit unsigned integer, register number.
//
// Returns:
//   word value read if success, -1 if error.
int ReadWord(int fd, uint8_t reg) {
  union i2c_smbus_data data;
  struct i2c_smbus_ioctl_data args;
  args.read_write = I2C_SMBUS_READ;
  args.command = reg;
  args.size = I2C_SMBUS_WORD_DATA;
  args.data = &data;

  if (PreconditionCheck(fd) != true) {
    return -1;
  }

  if (ioctl(fd, I2C_SMBUS, &args)) {
    printf("Error reading from bus: %s\n", strerror(errno));
    return -1;
  }
  printf("Read back from bus: 0x%x\n", data.word);
  return data.word;
}
