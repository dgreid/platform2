// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides abstraction for needed libc functionality that isn't included in
//! sys_util. Generally Sirenia code outside of this module shouldn't directly
//! interact with the libc package.
//!
//! TODO(b/162502718) Move this over to crosvm/sys_util

use std::fs::File;
use std::io;
use std::mem::{size_of, MaybeUninit};
use std::net::TcpStream;
use std::os::unix::io::{AsRawFd, FromRawFd, RawFd};
use std::ptr::null_mut;

use libc::{
    self, accept4, bind, c_int, connect, listen, sigfillset, sigprocmask, sigset_t, sockaddr_vm,
    socket, socklen_t, wait, AF_VSOCK, ECHILD, SIG_BLOCK, SIG_UNBLOCK, SOCK_CLOEXEC, SOCK_STREAM,
    SOMAXCONN, VMADDR_CID_ANY, VMADDR_CID_HOST, VMADDR_CID_HYPERVISOR,
};
use sys_util::handle_eintr_errno;

const VMADDR_CID_LOCAL: u32 = 1;

pub const VMADDR_PORT_ANY: u32 = libc::VMADDR_PORT_ANY;

const DEFAULT_SOCKADDR_VM: sockaddr_vm = sockaddr_vm {
    svm_family: 0,
    svm_reserved1: 0,
    svm_port: 0,
    svm_cid: 0,
    svm_zero: [0; 4],
};

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

fn get_vsock_stream() -> Result<RawFd, io::Error> {
    // This is safe because it doesn't take any references.
    let fd: RawFd = handle_eintr_errno!(unsafe { socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0) });
    if fd < 0 {
        Err(io::Error::last_os_error())
    } else {
        Ok(fd)
    }
}

fn new_vsock_addr(cid: VsockCid, port: u32) -> sockaddr_vm {
    let mut addr: sockaddr_vm = DEFAULT_SOCKADDR_VM.clone();

    addr.svm_family = AF_VSOCK as u16;
    addr.svm_port = port;
    addr.svm_cid = cid.into();

    addr
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

pub struct VsockStreamListener(File);

impl VsockStreamListener {
    pub fn new(cid: VsockCid, port: u32) -> Result<Self, io::Error> {
        // This is safe because the fd is valid and not associated with a rust
        // struct yet.
        let fd = unsafe { File::from_raw_fd(get_vsock_stream()?) };
        let addr = new_vsock_addr(cid, port);

        // This is safe because the passed structs will outlive the bind
        // call.
        if handle_eintr_errno!(unsafe {
            bind(
                fd.as_raw_fd(),
                &addr as *const _ as *const _,
                size_of::<sockaddr_vm>() as socklen_t,
            )
        }) != 0
        {
            return Err(io::Error::last_os_error());
        }

        // This is safe because the file descriptor is owned.
        if handle_eintr_errno!(unsafe { listen(fd.as_raw_fd(), SOMAXCONN) }) != 0 {
            return Err(io::Error::last_os_error());
        }

        Ok(VsockStreamListener(fd))
    }

    pub fn accept(&self) -> Result<(TcpStream, (VsockCid, u32)), io::Error> {
        let mut addr: sockaddr_vm = DEFAULT_SOCKADDR_VM.clone();
        let mut len: socklen_t = size_of::<sockaddr_vm>() as socklen_t;
        let stream: TcpStream;

        // This is safe because the passed structs will outlive the accept4
        // call and if the resulting file descriptor is valid it is wrapped
        // with a type that will close it when dropped.
        let fd: c_int = handle_eintr_errno!(unsafe {
            accept4(
                self.0.as_raw_fd(),
                &mut addr as *mut _ as *mut _,
                &mut len as *mut _,
                SOCK_CLOEXEC,
            )
        });
        if fd < 0 {
            return Err(io::Error::last_os_error());
        }

        // This is safe because the file descriptor is valid and isn't
        // associated with a rust struct yet.
        stream = unsafe { TcpStream::from_raw_fd(fd) };
        if size_of::<sockaddr_vm>() as socklen_t != len {
            return Err(io::Error::last_os_error());
        }
        Ok((stream, (VsockCid::from(addr.svm_cid), addr.svm_port)))
    }
}

pub fn vsock_connect(cid: VsockCid, port: u32) -> Result<TcpStream, io::Error> {
    let fd: RawFd = get_vsock_stream()?;
    let addr: sockaddr_vm = new_vsock_addr(cid, port);

    if handle_eintr_errno!(unsafe {
        // This is safe because the passed structs will outlive the connect
        // call.
        connect(
            fd,
            &addr as *const _ as *const _,
            size_of::<sockaddr_vm>() as socklen_t,
        )
    }) != 0
    {
        // This is safe because the file descriptor is valid and isn't
        // associated with a rust struct yet. It is used to close the
        // file descriptor.
        unsafe {
            File::from_raw_fd(fd);
        }
        return Err(io::Error::last_os_error());
    }

    // This is safe because the file descriptor is valid and isn't associated
    // with a rust struct yet.
    Ok(unsafe { TcpStream::from_raw_fd(fd) })
}
