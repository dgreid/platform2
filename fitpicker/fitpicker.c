// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define _GNU_SOURCE
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <libfdt.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char CONFIG_NODE_PATH[] = "/configurations";
static const char IMAGES_NODE_PATH[] = "/images";

static const char DATA_PROP_NAME[] = "data";
static const char COMPAT_PROP_NAME[] = "compatible";
static const char KERNEL_PROP_NAME[] = "kernel";
static const char DTB_PROP_NAME[] = "fdt";

typedef struct Image {
  // Name of the image node.
  const char* name;
  // Pointer to the image data.
  const void* data;
  // Length of the image.
  int len;
} Image;

typedef struct Config {
  // Name of the config node.
  const char* name;
  // Offset to the config in the FIT.
  int offset;

  // Information about the kernel image.
  Image kernel;
  // Information about the dtb image.
  Image dtb;

  // Used when comparing multiple configs that match a compat string.
  int rank;

  struct Config* next;
} Config;

// A linked list of configurations found in the FIT file.
static Config* fp_configs = NULL;

// Whether verbose output has been enabled.
static int fp_verbose = 0;

// Prints informational messages to stdout if verbose output has been enabled.
// Accepts the same arguments as printf.
static void __attribute__((__format__(__printf__, 1, 2)))
fp_log(const char* format, ...) {
  va_list args;

  if (fp_verbose) {
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
  }
}

// Usage information for the program.
static void fp_print_usage(void) {
  printf(
      "Usage: %s [options] <fit> <compat> <kernel> <dtb>\n"
      "\n"
      "Options:\n"
      "\t-v:\tVerbose output.\n"
      "\t-h:\tShow this help.\n"
      "\n"
      "fit:\tPath to the FIT file to pick a kernel/dtb from.\n"
      "compat:\tThe \"compat\" property to search for.\n"
      "kernel:\tFile name to write the chosen kernel to.\n"
      "dtb:\tFile name to write the chosen device tree to.\n",
      program_invocation_short_name);
}

// Allocate a buffer and read the contents of a file into it.
static const void* fp_read_file(const char* path) {
  const int fd = open(path, 0, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    warn("Failed to open %s for reading.", path);
    return NULL;
  }

  // Figure out how big the file is.
  struct stat stat_buf;
  if (fstat(fd, &stat_buf)) {
    warn("Failed to stat %s.", path);
    return NULL;
  }
  const off_t size = stat_buf.st_size;

  // Allocate a buffer.
  void* buf = malloc(size);
  if (!buf) {
    warn("Couldn't allocate %" PRIi64 " bytes.", size);
    return NULL;
  }

  // Read data into it until we hit an error or get the whole file.
  uint8_t* ptr = buf;
  off_t left = size;
  do {
    ssize_t ret = read(fd, ptr, left);
    if (ret < 0) {
      warn("Failed to read from %s.", path);
      free(buf);
      return NULL;
    }
    ptr += ret;
    left -= ret;
  } while (left);

  return buf;
}

// Write data in a buffer into a file.
static int fp_write_file(const char* path, const void* data, off_t len) {
  const int fd =
      open(path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0644);
  if (fd < 0) {
    warn("Failed to open %s for writing.", path);
    return -1;
  }

  // Write data until we hit an error or write the whole file.
  const uint8_t* ptr = data;
  off_t left = len;
  do {
    int ret = write(fd, ptr, left);
    if (ret < 0) {
      warn("Failed to write to %s.", path);
      return ret;
    }
    ptr += ret;
    left -= ret;
  } while (left);

  return len;
}

// Read an image with a given name from the image nodes in the FIT image.
// If lenp is non-NULL, it'll be set to the length of the image data.
static const void* fp_read_image(const void* fit_buf,
                                 const char* name,
                                 int* lenp) {
  // Find the parent node which houses all the image nodes.
  int images_offset = fdt_path_offset(fit_buf, IMAGES_NODE_PATH);
  if (images_offset < 0) {
    warn("Failed to find '%s' node.", IMAGES_NODE_PATH);
    return NULL;
  }

  // Find the node which holds the image we're looking for.
  int image_offset = fdt_subnode_offset(fit_buf, images_offset, name);
  if (image_offset < 0) {
    warn("Failed to find image %s.", name);
    return NULL;
  }

  // Get the "data" property from it which holds image data.
  const struct fdt_property* image_prop =
      fdt_get_property(fit_buf, image_offset, DATA_PROP_NAME, lenp);
  if (!image_prop) {
    warn("Failed to read image data for %s.", name);
    return NULL;
  }

  return image_prop->data;
}

// Find the "compatible" property at the root of a device tree.
static const char* fp_get_compat(const void* dtb_buf, int* lenp) {
  const struct fdt_property* compatible =
      fdt_get_property(dtb_buf, 0, COMPAT_PROP_NAME, lenp);
  if (!compatible) {
    warn("Couldn't find '%s' string.", COMPAT_PROP_NAME);
    return NULL;
  }

  return compatible->data;
}

// Find the offset of the first configuration node in the FIT image.
static int fp_first_config(const void* fit_buf) {
  // Find the parent node which houses all the configurations.
  int configs_offset = fdt_path_offset(fit_buf, CONFIG_NODE_PATH);
  if (configs_offset < 0) {
    warn("Failed to find '%s' in the FIT.", CONFIG_NODE_PATH);
    return configs_offset;
  }

  // Get the first configuration node.
  int config_offset = fdt_first_subnode(fit_buf, configs_offset);
  if (config_offset < 0)
    return 0;

  return config_offset;
}

// Find the offset of the next configuration node after the one at "offset"
// in a FIT image.
static int fp_next_config(const void* fit_buf, int offset) {
  offset = fdt_next_subnode(fit_buf, offset);
  if (offset < 0)
    return 0;

  return offset;
}

// Fill in info about an image associated with a configuration in the FIT.
static int fp_init_image(const void* fit_buf,
                         Config* config,
                         const char* prop_name,
                         Image* image) {
  // Find the property which holds the name of the image of this type.
  const struct fdt_property* prop =
      fdt_get_property(fit_buf, config->offset, prop_name, NULL);
  if (!prop) {
    warn("Failed to find %s image name for config %s.", prop_name,
         config->name);
    return -1;
  }
  image->name = prop->data;

  // Read the image itself from the FIT image.
  image->data = fp_read_image(fit_buf, image->name, &image->len);
  if (!image->data)
    return -1;

  return 0;
}

// Initialize and fill out a structure describing a configuration in the FIT.
static Config* fp_init_config(const void* fit_buf, int offset) {
  Config* config = malloc(sizeof(Config));
  config->next = NULL;
  config->rank = -1;
  config->offset = offset;

  // Get the name of this config node.
  config->name = fdt_get_name(fit_buf, offset, NULL);

  // Read in information for the kernel and dtb images for this config.
  if (fp_init_image(fit_buf, config, KERNEL_PROP_NAME, &config->kernel) < 0 ||
      fp_init_image(fit_buf, config, DTB_PROP_NAME, &config->dtb) < 0) {
    free(config);
    return NULL;
  }

  return config;
}

int main(int argc, char* const argv[]) {
  int c;
  while ((c = getopt(argc, argv, "vh")) != -1) {
    switch (c) {
      case 'v':
        fp_verbose = 1;
        break;
      case 'h':
        fp_print_usage();
        return 0;
    }
  }

  if (argc - optind != 4) {
    fp_print_usage();
    return 1;
  }

  const char* fit_path = argv[optind + 0];
  const char* compat = argv[optind + 1];
  const char* kernel_path = argv[optind + 2];
  const char* dtb_path = argv[optind + 3];

  const void* fit_buf = fp_read_file(fit_path);
  if (!fit_buf)
    return 1;

  int offset = fp_first_config(fit_buf);
  if (offset < 0)
    return 1;

  // Read all the configurations from the FIT into a linked list.
  for (Config** end = &fp_configs; offset; end = &(*end)->next) {
    *end = fp_init_config(fit_buf, offset);
    if (!*end)
      return 1;
    offset = fp_next_config(fit_buf, offset);
  }

  // Go through all the configurations and find the "best" match to the
  // compat string we were given. Best is defined to be the one whose
  // most specific (earliest) compatible property element matches the
  // compat string.
  Config* best = NULL;
  for (Config* config = fp_configs; config; config = config->next) {
    fp_log("Config %s: kernel = %s, dtb = %s.\n", config->name,
           config->kernel.name, config->dtb.name);

    int compat_len;
    const char* dtb_compat = fp_get_compat(config->dtb.data, &compat_len);
    if (!dtb_compat)
      return 1;

    for (int compat_idx = 0, left = compat_len, len; left;
         compat_idx++, dtb_compat += len, left -= len) {
      fp_log("  Compatible: %s", dtb_compat);
      len = strlen(dtb_compat) + 1;

      if (!strcmp(dtb_compat, compat)) {
        fp_log(" (match)");
        if (config->rank < 0) {
          config->rank = compat_idx;
          if (!best || config->rank < best->rank)
            best = config;
        }
      }
      fp_log("\n");
    }
  }

  if (!best) {
    warn("\nNo match found!\n");
    return 1;
  } else {
    fp_log("\nBest match is config %s.\n\n", best->name);
  }

  // Now that we've picked a configuration, write the kernel and device
  // tree associated with it to the paths provided.
  fp_write_file(kernel_path, best->kernel.data, best->kernel.len);
  fp_write_file(dtb_path, best->dtb.data, best->dtb.len);

  return 0;
}
