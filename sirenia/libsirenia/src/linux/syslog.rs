// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A syslog server interface for use with EventMultiplexer.

use std::boxed::Box;
use std::cell::RefCell;
use std::fs::remove_file;
use std::io::{Error as IoError, Read};
use std::mem::replace;
use std::os::unix::io::{AsRawFd, RawFd};
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::{Path, PathBuf};
use std::rc::Rc;

use libchromeos::linux::{getpid, gettid};
use libchromeos::scoped_path::get_temp_path;
use sys_util::{self, error, handle_eintr, warn};

use super::events::{AddEventSourceMutator, EventSource, Mutator, RemoveFdMutator};

pub const SYSLOG_PATH: &str = "/dev/log";

/// The maximum buffer size for a partial message.
pub const MAX_MESSAGE: usize = 4096;

/// A receiver of syslog messages. Note that one or more messages may be received together.
pub trait SyslogReceiver {
    fn receive(&self, data: String);
}

/// A trait that can be used along with RefCell to be used as a SyslogReceiver.
pub trait SyslogReceiverMut {
    fn receive(&mut self, data: String);
}

impl<R: SyslogReceiverMut> SyslogReceiver for RefCell<R> {
    fn receive(&self, data: String) {
        self.borrow_mut().receive(data);
    }
}

/// Encapsulates a connection with a a syslog client.
struct SyslogClient {
    stream: UnixStream,
    receiver: Rc<dyn SyslogReceiver>,
    partial_msg: String,
}

impl SyslogClient {
    fn new(stream: UnixStream, receiver: Rc<dyn SyslogReceiver>) -> Self {
        SyslogClient {
            stream,
            receiver,
            partial_msg: String::new(),
        }
    }

    fn receive_raw(&mut self, data: &[u8]) {
        let parsed = String::from_utf8_lossy(data);
        if let Some(last_newline_index) = parsed.rfind('\n') {
            let mut messages = replace(
                &mut self.partial_msg,
                parsed[(last_newline_index + 1)..].to_string(),
            );
            if self.partial_msg.len() < MAX_MESSAGE {
                messages.push_str(&parsed[..=last_newline_index]);
            } else {
                messages.push('\n');
                // If the message was truncated, make sure messages aren't dropped.
                if let Some(first_newline_index) = parsed.find('\n') {
                    if first_newline_index != last_newline_index {
                        messages.push_str(&parsed[(first_newline_index + 1)..=last_newline_index])
                    }
                } else {
                    unreachable!();
                }
            }
            self.receiver.receive(messages);
        } else if self.partial_msg.len() < MAX_MESSAGE {
            // This logic truncates the buffered partial message at the buffer size to
            // make a memory exhaustion DoS more difficult.
            let leftover = MAX_MESSAGE - self.partial_msg.len();
            if leftover >= data.len() {
                self.partial_msg.push_str(parsed.as_ref())
            } else {
                warn!("truncated syslog message.");
                self.partial_msg.push_str(&parsed[0..leftover])
            }
        }
    }
}

impl AsRawFd for SyslogClient {
    fn as_raw_fd(&self) -> RawFd {
        self.stream.as_raw_fd()
    }
}

impl EventSource for SyslogClient {
    fn on_event(&mut self) -> Result<Option<Box<dyn Mutator>>, String> {
        let mut buffer: [u8; MAX_MESSAGE] = [0; MAX_MESSAGE];
        Ok(match handle_eintr!(self.stream.read(&mut buffer)) {
            Ok(len) => {
                self.receive_raw(&buffer[..len]);
                None
            }
            Err(_) => Some(Box::new(RemoveFdMutator(self.stream.as_raw_fd()))),
        })
    }
}

/// Encapsulates a unix socket listener for a syslog server that accepts client connections.
pub struct Syslog {
    listener: UnixListener,
    receiver: Rc<dyn SyslogReceiver>,
}

impl Syslog {
    pub fn get_log_path() -> PathBuf {
        if cfg!(test) {
            // NOTE this changes based on thread id, so it should be different across concurrent
            // test cases.
            let path = get_temp_path(None).join(&SYSLOG_PATH[1..]);
            // Max Unix socket path is >100 and varies between OSes.
            if path.to_string_lossy().len() <= 100 {
                path
            } else {
                Path::new("/tmp")
                    .join(format!("test-{}-{}", getpid(), gettid()))
                    .join(&SYSLOG_PATH[1..])
            }
        } else {
            Path::new(SYSLOG_PATH).to_path_buf()
        }
    }

    /// Return true if there is already a syslog socket open at SYSLOG_PATH.
    pub fn is_syslog_present() -> bool {
        Self::get_log_path().exists()
    }

    /// Binds a new unix socket listener at the SYSLOG_PATH.
    pub fn new(receiver: Rc<dyn SyslogReceiver>) -> Result<Self, IoError> {
        Ok(Syslog {
            listener: UnixListener::bind(Self::get_log_path())?,
            receiver,
        })
    }
}

/// Cleanup the unix socket by removing SYSLOG_PATH whenever the Syslog is dropped.
impl Drop for Syslog {
    fn drop(&mut self) {
        if let Err(e) = remove_file(Self::get_log_path()) {
            if e.kind() != std::io::ErrorKind::NotFound {
                eprintln!("Failed to cleanup syslog: {:?}", e);
            }
        }
    }
}

impl AsRawFd for Syslog {
    fn as_raw_fd(&self) -> RawFd {
        self.listener.as_raw_fd()
    }
}

/// Creates a EventSource that adds any accept connections and returns a Mutator that will add the
/// client connection to the EventMultiplexer when applied.
impl EventSource for Syslog {
    fn on_event(&mut self) -> Result<Option<Box<dyn Mutator>>, String> {
        Ok(Some(match handle_eintr!(self.listener.accept()) {
            Ok((instance, _)) => Box::new(AddEventSourceMutator(Some(Box::new(
                SyslogClient::new(instance, self.receiver.clone()),
            )))),
            Err(e) => {
                error!("syslog socket error: {:?}", e);
                Box::new(RemoveFdMutator(self.listener.as_raw_fd()))
            }
        }))
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    use std::io::Write;
    use std::sync::{Arc, Barrier};
    use std::thread::spawn;

    use libchromeos::scoped_path::ScopedPath;

    use super::super::events::EventMultiplexer;

    struct TestReciever(Vec<String>);

    impl AsRef<Vec<String>> for TestReciever {
        fn as_ref(&self) -> &Vec<String> {
            &self.0
        }
    }

    impl SyslogReceiverMut for TestReciever {
        fn receive(&mut self, data: String) {
            self.0.push(data);
        }
    }

    fn get_test_receiver() -> Rc<RefCell<TestReciever>> {
        Rc::new(RefCell::new(TestReciever(Vec::new())))
    }

    fn get_test_client(receiver: Rc<RefCell<TestReciever>>) -> SyslogClient {
        let connect_path = Syslog::get_log_path();
        let test_path = ScopedPath::create(connect_path.parent().unwrap()).unwrap();
        assert!(test_path.exists());
        let listener = UnixListener::bind(&connect_path).unwrap();
        let server = spawn(move || {
            let mut syslog = Syslog {
                listener,
                receiver: get_test_receiver(),
            };
            syslog.on_event().unwrap();
        });
        let client = SyslogClient::new(UnixStream::connect(&connect_path).unwrap(), receiver);
        server.join().unwrap();
        client
    }

    #[test]
    fn syslog_issyslogpresent_false() {
        assert!(!Syslog::is_syslog_present());
    }

    #[test]
    fn syslog_issyslogpresent_true() {
        let test_path = ScopedPath::create(Syslog::get_log_path()).unwrap();
        assert!(test_path.exists());
        assert!(Syslog::is_syslog_present());
    }

    #[test]
    fn syslog_new_fail() {
        let test_path = ScopedPath::create(Syslog::get_log_path()).unwrap();
        assert!(test_path.exists());

        let receiver = get_test_receiver();
        assert!(Syslog::new(receiver).is_err());
    }

    #[test]
    fn syslog_new_drop() {
        let log_path = Syslog::get_log_path();
        let test_path = ScopedPath::create(log_path.parent().unwrap()).unwrap();
        assert!(test_path.exists());
        assert!(!Syslog::is_syslog_present());

        let receiver = get_test_receiver();
        {
            let _syslog = Syslog::new(receiver).unwrap();
            assert!(Syslog::is_syslog_present());
        }
        assert!(!Syslog::is_syslog_present());
    }

    #[test]
    fn syslogclient_receiveraw_partialonly() {
        let test_data = "test_data";
        let receiver = get_test_receiver();
        let mut client = get_test_client(receiver.clone());

        assert!(receiver.borrow().as_ref().is_empty());
        client.receive_raw(test_data.as_bytes());
        assert!(receiver.borrow().as_ref().is_empty());
        assert_eq!(client.partial_msg, test_data);
    }

    #[test]
    fn syslogclient_receiveraw_bufferonly() {
        let test_data = "test_data\n";
        let receiver = get_test_receiver();
        let mut client = get_test_client(receiver.clone());

        assert!(receiver.borrow().as_ref().is_empty());
        client.receive_raw(test_data.as_bytes());
        assert_eq!(receiver.borrow().as_ref().len(), 1);
        assert_eq!(receiver.borrow().as_ref()[0], test_data);
        assert!(client.partial_msg.is_empty());
    }

    #[test]
    fn syslogclient_receiveraw_bufferandpartial() {
        let message = "test_data\n";
        let partial = "partial";
        let receiver = get_test_receiver();
        let mut client = get_test_client(receiver.clone());

        assert!(receiver.borrow().as_ref().is_empty());
        client.receive_raw(format!("{}{}", message, partial).as_bytes());
        assert_eq!(receiver.borrow().as_ref().len(), 1);
        assert_eq!(receiver.borrow().as_ref()[0], message);
        assert_eq!(client.partial_msg, partial);
    }

    #[test]
    fn syslogclient_receiveraw_partialoverflow() {
        let message: Vec<u8> = vec![' ' as u8; MAX_MESSAGE + 1];
        let receiver = get_test_receiver();
        let mut client = get_test_client(receiver.clone());

        assert!(receiver.borrow().as_ref().is_empty());
        client.receive_raw(&message);
        assert!(receiver.borrow().as_ref().is_empty());
        assert_eq!(client.partial_msg.len(), MAX_MESSAGE);

        client.receive_raw("ZZZ".as_bytes());
        assert!(receiver.borrow().as_ref().is_empty());
        assert_eq!(client.partial_msg.len(), MAX_MESSAGE);
        assert!(client.partial_msg.rfind('Z').is_none());
    }

    #[test]
    fn syslog_eventmultiplexer_integration() {
        let log_path = Syslog::get_log_path();
        let test_path = ScopedPath::create(log_path.parent().unwrap()).unwrap();
        assert!(test_path.exists());
        assert!(!Syslog::is_syslog_present());

        let receiver = get_test_receiver();
        let syslog = Syslog::new(receiver.clone()).unwrap();
        assert!(Syslog::is_syslog_present());
        let mut context = EventMultiplexer::new().unwrap();
        context.add_event(Box::new(syslog)).unwrap();

        let connect_path = log_path.clone();
        let local_check = Arc::new(Barrier::new(2));
        let client_check = Arc::clone(&local_check);
        let client = spawn(move || {
            let mut stream = UnixStream::connect(connect_path).unwrap();
            stream.write_all("Test Data\n".as_bytes()).unwrap();

            // Make sure the read happens before dropping the socket.
            client_check.wait();
        });

        // Check Syslog::on_event().
        context.run_once().unwrap();
        assert_eq!(context.len(), 2);

        // Check SyslogClient::on_event().
        context.run_once().unwrap();
        assert_eq!(receiver.as_ref().borrow().as_ref().len(), 1);
        local_check.wait();

        // Check cleanup.
        client.join().unwrap();
        context.run_once().unwrap();
        assert_eq!(context.len(), 1);
    }
}
