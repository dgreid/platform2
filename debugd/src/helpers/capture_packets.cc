// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// packet capture helper.  This initiates packet capture on a device
// and stores the output pcap file to the specified destination.

#include <signal.h>
#include <stdio.h>

#include <chromeos/libminijail.h>
#include <pcap.h>

#define RECEIVE_PACKET_SIZE 2048
#define PACKET_TIMEOUT_MS 1000

int perform_capture(char* device, char* output_file) {
  char buf[RECEIVE_PACKET_SIZE];
  const int promiscuous = 0;
  char errbuf[PCAP_ERRBUF_SIZE];
  pcap_t* pcap = pcap_open_live(device, sizeof(buf), promiscuous,
                                PACKET_TIMEOUT_MS, errbuf);
  if (pcap == nullptr) {
    fprintf(stderr, "Could not open capture handle.\n");
    return -1;
  }

  pcap_dumper_t* dumper = pcap_dump_open(pcap, output_file);
  if (dumper == nullptr) {
    fprintf(stderr, "Could not open dump file.\n");
    return -1;
  }

  // Now that we have all our handles open, drop privileges.
  struct minijail* j = minijail_new();
  minijail_change_user(j, "debugd");
  minijail_change_group(j, "debugd");
  minijail_enter(j);

  int packet_count = 0;
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGTERM);
  sigaddset(&sigset, SIGINT);
  sigprocmask(SIG_BLOCK, &sigset, nullptr);
  while (sigpending(&sigset) == 0) {
    if (sigismember(&sigset, SIGTERM) || sigismember(&sigset, SIGINT)) {
      break;
    }
    struct pcap_pkthdr header;
    const unsigned char* packet = pcap_next(pcap, &header);
    if (packet == nullptr || header.len == 0) {
      continue;
    }
    ++packet_count;
    pcap_dump(reinterpret_cast<u_char*>(dumper), &header, packet);
  }

  pcap_close(pcap);
  pcap_dump_close(dumper);

  printf("Exiting after %d captured packets\n", packet_count);

  return 0;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <device> <output_file>\n", argv[0]);
    return 1;
  }

  return perform_capture(argv[1], argv[2]);
}
