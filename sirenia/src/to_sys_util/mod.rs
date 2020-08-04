// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides abstraction for needed libc functionality that isn't included in
//! sys_util. Generally Sirenia code outside of this module shouldn't directly
//! interact with the libc package.
//!
//! TODO(b/162502718) Move this over to crosvm/sys_util

use std::io;
use std::mem::MaybeUninit;
use std::ptr::null_mut;

use libc::{
    self, c_int, sigfillset, sigprocmask, sigset_t, wait, ECHILD, SIG_BLOCK, SIG_UNBLOCK,
    VMADDR_CID_ANY, VMADDR_CID_HOST, VMADDR_CID_HYPERVISOR,
};

const VMADDR_CID_LOCAL: u32 = 1;

pub fn errno() -> c_int {
    io::Error::last_os_error().raw_os_error().unwrap()
}

pub fn wait_for_child() -> bool {
    let mut ret: c_int = 0;
    // This is safe because it merely blocks execution until a process
    // life-cycle event occurs, or there are no child processes to wait on.
    if unsafe { wait(&mut ret) } == -1 && errno() == ECHILD {
        return false;
    }

    true
}

pub fn block_all_signals() {
    let mut signal_set: sigset_t;
    // This is safe as long as nothing else is depending on receiving a signal
    // to guarantee safety.
    unsafe {
        signal_set = MaybeUninit::zeroed().assume_init();
        // Block signals since init should not die or return.
        sigfillset(&mut signal_set);
        sigprocmask(SIG_BLOCK, &signal_set, null_mut());
    }
}

pub fn unblock_all_signals() {
    let mut signal_set: sigset_t;
    // This is safe because it doesn't allocate or free any structures.
    unsafe {
        signal_set = MaybeUninit::zeroed().assume_init();
        // Block signals since init should not die or return.
        sigfillset(&mut signal_set);
        sigprocmask(SIG_UNBLOCK, &signal_set, null_mut());
    }
}

/// Forks the process and returns the child pid or 0 for the child process.
///
/// # Safety
///
/// This is only safe if the open file descriptors are intended to be cloned
/// into the child process. The child should explicitly close any file
/// descriptors that are not intended to be kept open.
pub unsafe fn fork() -> Result<i32, io::Error> {
    let ret: c_int = libc::fork();
    if ret < 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(ret)
    }
}

#[derive(Debug, Copy, Clone)]
pub enum VsockCid {
    Any,
    Hypervisor,
    Local,
    Host,
    Cid(u32),
}

impl From<u32> for VsockCid {
    fn from(c: u32) -> Self {
        match c {
            VMADDR_CID_ANY => VsockCid::Any,
            VMADDR_CID_HYPERVISOR => VsockCid::Hypervisor,
            VMADDR_CID_LOCAL => VsockCid::Local,
            VMADDR_CID_HOST => VsockCid::Host,
            _ => VsockCid::Cid(c),
        }
    }
}

impl Into<u32> for VsockCid {
    fn into(self) -> u32 {
        match self {
            VsockCid::Any => VMADDR_CID_ANY,
            VsockCid::Hypervisor => VMADDR_CID_HYPERVISOR,
            VsockCid::Local => VMADDR_CID_LOCAL,
            VsockCid::Host => VMADDR_CID_HOST,
            VsockCid::Cid(c) => c,
        }
    }
}
