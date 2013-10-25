// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERF_TEST_FILES_
#define PERF_TEST_FILES_

namespace perf_test_files {

// The following perf data contains the following event types, as passed to
// perf record via the -e option:
// - cycles
// - instructions
// - cache-references
// - cache-misses
// - branches
// - branch-misses

const char* kPerfDataFiles[] = {
  // Obtained with "perf record -- echo > /dev/null"
  "perf.data.singleprocess",

  // Obtained with "perf record -a -- sleep $N", for N in {0, 1, 5}.
  "perf.data.systemwide.0",
  "perf.data.systemwide.1",
  "perf.data.systemwide.5",

  // Obtained with "perf record -a -- sleep $N", for N in {0, 1, 5}.
  // While in the background, this loop is running:
  //   while true; do ls > /dev/null; done
  "perf.data.busy.0",
  "perf.data.busy.1",
  "perf.data.busy.5",

  // Obtained with "perf record -a -- sleep 2"
  // While in the background, this loop is running:
  //   while true; do restart powerd; sleep .2; done
  "perf.data.forkexit",

  // Obtained with "perf record -a -g -- sleep 2"
  "perf.data.callgraph",
  // Obtained with "perf record -a -b -- sleep 2"
  "perf.data.branch",
  // Obtained with "perf record -a -g -b -- sleep 2"
  "perf.data.callgraph_and_branch",

  // Obtained with "perf record -a -R -- sleep 2"
  "perf.data.raw",
  // Obtained with "perf record -a -R -g -b -- sleep 2"
  "perf.data.raw_callgraph_branch",

  // Data from other architectures.
  "perf.data.i686",     // 32-bit x86
  "perf.data.armv7",    // ARM v7

  // Same as above, obtained from a system running kernel-next.
  "perf.data.singleprocess.next",
  "perf.data.systemwide.0.next",
  "perf.data.systemwide.1.next",
  "perf.data.systemwide.5.next",
  "perf.data.busy.0.next",
  "perf.data.busy.1.next",
  "perf.data.busy.5.next",
  "perf.data.forkexit.next",
  "perf.data.callgraph.next",
  "perf.data.branch.next",
  "perf.data.callgraph_and_branch.next",

  // Obtained from a system that uses NUMA topology.
  "perf.data.numatopology",

  // Data which contains events with non-consecutive event ids.
  // Events are cycles (id 0) and branch-misses (id 5).
  "perf.data.cycles_and_branch",

  // This test first mmap()s a DSO, then fork()s to copy the mapping to the
  // child and then modifies the mapping by mmap()ing a DSO on top of the old
  // one. It then records SAMPLEs events in the child. It ensures the SAMPLEs in
  // the child are attributed to the first DSO that was mmap()ed, not the second
  // one.
  "perf.data.remmap"
};

const char* kPerfPipedDataFiles[] = {
  "perf.data.piped.host",
  "perf.data.piped.target",
  "perf.data.piped.target.throttled",
  // From system running kernel-next.
  "perf.data.piped.target.next",

  // Piped data that contains hardware and software events.
  // HW events are cycles and branch-misses, SW event is cpu-clock.
  "perf.data.piped.hw_and_sw",

  // Piped data with extra data at end.
  "perf.data.piped.extrabyte",
  "perf.data.piped.extradata",
};

}  // namespace perf_test_files

#endif  // PERF_TEST_FILES_
