// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a rework of crosvm/devices/src/utils/event_loop.rs for single threaded use.
// Notable changes:
//   * FailHandles were removed
//   * The Weak references to callbacks were upgraded to ownership. This enables functionality
//     like socket servers where the callback struct is owned by the event_loop and is dropped when
//     the fd is removed from the event loop.
//   * EventLoop::start(...) was split into EventMultiplexer::new() and
//     EventMultiplexer::run_once(). The initialization was put in EventMultiplexer::new(), and the
//     thread and loop were removed replaced with a single wait call in
//     EventMultiplexer::run_once().
//   * To make this work with a single thread without mutexes, Mutators were introduced as the
//     return type for on_event(). The mutator enables actions like removing a fd from the
//     EventMultiplexer on a recoverable error, or adding a new EventSource when a Listener accepts
//     a new stream.

use std::boxed::Box;
use std::collections::BTreeMap;
use std::fmt::{self, Display};
use std::os::unix::io::{AsRawFd, RawFd};
use std::result::Result as StdResult;

use sys_util::{error, warn, Error as SysError, PollContext, PollToken, WatchingEvents};

#[derive(Debug)]
pub enum Error {
    CreatePollContext(SysError),
    PollContextAddFd(SysError),
    PollContextDeleteFd(SysError),
    PollContextWait(SysError),
    OnEvent(String),
    OnMutate(String),
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        match self {
            CreatePollContext(e) => write!(f, "failed to create poll context: {}", e),
            PollContextAddFd(e) => write!(f, "failed to add fd to poll context: {}", e),
            PollContextDeleteFd(e) => write!(f, "failed to delete fd from poll context: {}", e),
            PollContextWait(e) => {
                write!(f, "failed to wait for events using the poll context: {}", e)
            }
            OnEvent(s) => write!(f, "event failed: {}", s),
            OnMutate(s) => write!(f, "mutate failed: {}", s),
        }
    }
}

pub type Result<T> = std::result::Result<T, Error>;

/// Fd is a wrapper of RawFd. It implements AsRawFd trait and PollToken trait for RawFd.
/// It does not own the fd, thus won't close the fd when dropped.
struct Fd(pub RawFd);
impl AsRawFd for Fd {
    fn as_raw_fd(&self) -> RawFd {
        self.0
    }
}

impl PollToken for Fd {
    fn as_raw_token(&self) -> u64 {
        self.0 as u64
    }

    fn from_raw_token(data: u64) -> Self {
        Fd(data as RawFd)
    }
}

pub struct EventMultiplexer {
    poll_ctx: PollContext<Fd>,
    handlers: BTreeMap<RawFd, Box<dyn EventSource>>,
}

pub trait Mutator {
    fn mutate(&mut self, event_loop: &mut EventMultiplexer) -> std::result::Result<(), String>;
}

/// Interface for event handler.
pub trait EventSource: AsRawFd {
    /// Callback to be executed when the event loop encounters an event for this handler.
    fn on_event(&mut self) -> std::result::Result<Option<Box<dyn Mutator>>, String>;
}

/// Additional abstraction on top of PollContext to make it possible to multiplex listeners,
/// streams, (anything with AsRawFd) on a single thread.
impl EventMultiplexer {
    /// Initialize the EventMultiplexer.
    pub fn new() -> Result<EventMultiplexer> {
        let handlers: BTreeMap<RawFd, Box<dyn EventSource>> = BTreeMap::new();
        let poll_ctx: PollContext<Fd> = PollContext::new().map_err(Error::CreatePollContext)?;

        Ok(EventMultiplexer { poll_ctx, handlers })
    }

    /// Wait until there are events to process. Then, process them. If an error is returned, there
    /// may still events to process.
    pub fn run_once(&mut self) -> Result<()> {
        let mut to_remove: Vec<RawFd> = Vec::new();
        let mut to_read: Vec<RawFd> = Vec::new();
        for event in self.poll_ctx.wait().map_err(Error::PollContextWait)?.iter() {
            if event.hungup() {
                &mut to_remove
            } else {
                &mut to_read
            }
            .push(event.token().as_raw_fd());
        }

        for fd in to_read {
            let mutator: Option<Box<dyn Mutator>> = match self.handlers.get_mut(&fd) {
                Some(cb) => cb.on_event().map_err(Error::OnEvent)?,
                None => {
                    warn!("callback for fd {} already removed", fd);
                    continue;
                }
            };

            if let Some(mut m) = mutator {
                m.mutate(self).map_err(Error::OnMutate)?;
            }
        }

        for fd in to_remove {
            self.remove_event_for_fd(&Fd(fd))
                .map_err(|err| {
                    error!("failed to remove event fd: {:?}", err);
                })
                .ok();
        }

        Ok(())
    }

    /// Add a new event to multiplexer. The handler will be invoked when `event` happens on `fd`.
    pub fn add_event(&mut self, handler: Box<dyn EventSource>) -> Result<()> {
        let fd = handler.as_raw_fd();
        self.handlers.insert(fd, handler);
        // This might fail due to epoll syscall. Check epoll_ctl(2).
        self.poll_ctx
            .add_fd_with_events(&Fd(fd), WatchingEvents::empty().set_read(), Fd(fd))
            .map_err(Error::PollContextAddFd)
    }

    /// Stops listening for events for this `fd`. This function returns an error if it fails, or the
    /// removed EventSource if it succeeds.
    ///
    /// EventMultiplexer does not guarantee all events for `fd` is handled.
    pub fn remove_event_for_fd(&mut self, fd: &dyn AsRawFd) -> Result<Box<dyn EventSource>> {
        // This might fail due to epoll syscall. Check epoll_ctl(2).
        self.poll_ctx
            .delete(fd)
            .map_err(Error::PollContextDeleteFd)?;
        Ok(self.handlers.remove(&fd.as_raw_fd()).unwrap())
    }

    /// Returns true if there are no event sources registered.
    pub fn is_empty(&self) -> bool {
        self.handlers.is_empty()
    }
}

/// Adds the specified EventSource from the EventMultiplexer when the mutator is executed.
pub struct AddEventSourceMutator(pub Option<Box<dyn EventSource>>);

impl Mutator for AddEventSourceMutator {
    fn mutate(&mut self, event_loop: &mut EventMultiplexer) -> StdResult<(), String> {
        match std::mem::replace(&mut self.0, None) {
            Some(b) => event_loop
                .add_event(b)
                .map_err(|e| format!("failed to add fd: {:?}", e)),
            None => Err("AddHandlerMutator::mutate called for empty fd".to_string()),
        }
    }
}

/// Removes the specified RawFd from the EventMultiplexer when the mutator is executed.
pub struct RemoveFdMutator(pub RawFd);

impl Mutator for RemoveFdMutator {
    fn mutate(&mut self, event_loop: &mut EventMultiplexer) -> StdResult<(), String> {
        match event_loop.remove_event_for_fd(self) {
            Ok(_) => Ok(()),
            Err(e) => Err(format!("failed to remove fd: {:?}", e)),
        }
    }
}

impl AsRawFd for RemoveFdMutator {
    fn as_raw_fd(&self) -> RawFd {
        self.0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::cell::RefCell;
    use std::fs::File;
    use std::rc::Rc;

    use std::io::{Read, Write};
    use sys_util::{pipe, EventFd};

    struct EventMultiplexerTestHandler {
        val: Rc<RefCell<u8>>,
        evt: File,
    }

    impl AsRawFd for EventMultiplexerTestHandler {
        fn as_raw_fd(&self) -> i32 {
            self.evt.as_raw_fd()
        }
    }

    impl EventSource for EventMultiplexerTestHandler {
        fn on_event(&mut self) -> std::result::Result<Option<Box<dyn Mutator>>, String> {
            let mut buf: [u8; 1] = [0; 1];
            self.evt.read_exact(&mut buf).unwrap();
            *self.val.borrow_mut() += 1;
            Ok(None)
        }
    }

    #[test]
    fn event_multiplexer_test() {
        let mut l = EventMultiplexer::new().unwrap();
        let (r, mut w) = pipe(false /*close_on_exec*/).unwrap();
        let counter: Rc<RefCell<u8>> = Rc::new(RefCell::new(0));
        let h = EventMultiplexerTestHandler {
            val: Rc::clone(&counter),
            evt: r,
        };
        l.add_event(Box::new(h)).unwrap();

        // Check write.
        let buf: [u8; 1] = [1; 1];
        w.write_all(&buf).unwrap();
        l.run_once().unwrap();
        assert_eq!(*counter.borrow(), 1);

        // Check hangup.
        drop(w);
        l.run_once().unwrap();
        assert!(l.handlers.is_empty());
    }

    struct MutatorTestHandler(EventFd);

    impl AsRawFd for MutatorTestHandler {
        fn as_raw_fd(&self) -> i32 {
            self.0.as_raw_fd()
        }
    }

    impl EventSource for MutatorTestHandler {
        fn on_event(&mut self) -> std::result::Result<Option<Box<dyn Mutator>>, String> {
            Ok(None)
        }
    }

    #[test]
    fn add_event_source_mutator_test() {
        let mut l = EventMultiplexer::new().unwrap();
        let h = MutatorTestHandler(EventFd::new().unwrap());

        assert!(l.handlers.is_empty());
        AddEventSourceMutator(Some(Box::new(h)))
            .mutate(&mut l)
            .unwrap();
        assert!(!l.handlers.is_empty());
    }

    #[test]
    fn remove_fd_mutator_test() {
        let mut l = EventMultiplexer::new().unwrap();
        let h = MutatorTestHandler(EventFd::new().unwrap());
        let mut m = RemoveFdMutator(h.as_raw_fd());
        l.add_event(Box::new(h)).unwrap();

        assert!(!l.handlers.is_empty());
        m.mutate(&mut l).unwrap();
        assert!(l.handlers.is_empty());
    }
}
