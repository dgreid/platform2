// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles the transport abstractions for Sirenia. This allows communication
//! between Dugong and Trichechus to be tested locally without needing to use
//! vsock, or even IP sockets (if pipes are used). It also allows for
//! implementing communication for cases were vsock isn't available or
//! appropriate.

use std::boxed::Box;
use std::fmt::{self, Debug, Display};
use std::io::{self, Read, Write};
use std::iter::Iterator;
use std::marker::Send;
use std::net::{SocketAddr, TcpListener, TcpStream, ToSocketAddrs};

use core::mem::replace;
use libchromeos::vsock::{self, VsockListener, VsockStream};
use sys_util::{handle_eintr, pipe};

use super::to_sys_util::VsockCid;

const DEFAULT_PORT: u32 = 5552;

#[derive(Debug)]
pub enum Error {
    /// Failed to clone a fd.
    Clone(io::Error),
    /// Failed to bind a socket.
    Bind(io::Error),
    /// Failed to get the socket address.
    GetAddress(io::Error),
    /// Failed to accept the incoming connection.
    Accept(io::Error),
    /// Failed to parse the socket address.
    ParseAddress(Option<io::Error>),
    /// Failed to connect to the socket address.
    Connect(io::Error),
    /// Failed to construct the pipe.
    Pipe(sys_util::Error),
    /// The pipe transport was in the wrong state to complete the requested
    /// operation.
    InvalidState,
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        match self {
            Clone(e) => write!(f, "failed to clone fd: {}", e),
            Bind(e) => write!(f, "failed to bind: {}", e),
            GetAddress(e) => write!(f, "failed to get the socket address: {}", e),
            Accept(e) => write!(f, "failed to accept connection: {}", e),
            ParseAddress(e) => match e {
                Some(i) => write!(f, "failed to parse the socket address: {}", i),
                None => write!(f, "failed to parse the socket address"),
            },
            Connect(e) => write!(f, "failed to connect: {}", e),
            Pipe(e) => write!(f, "failed to construct the pipe: {}", e),
            InvalidState => write!(f, "pipe transport was in the wrong state"),
        }
    }
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

/// An abstraction wrapper to support the receiving side of a transport method.
pub trait ReadDebugSend: Read + Debug + Send {}
impl<T: Read + Debug + Send> ReadDebugSend for T {}
/// An abstraction wrapper to support the sending side of a transport method.
pub trait WriteDebugSend: Write + Debug + Send {}
impl<T: Write + Debug + Send> WriteDebugSend for T {}

/// Wraps a complete transport method, both sending and receiving.
#[derive(Debug)]
pub struct Transport(pub Box<dyn ReadDebugSend>, pub Box<dyn WriteDebugSend>);

impl Into<Transport> for (Box<dyn ReadDebugSend>, Box<dyn WriteDebugSend>) {
    fn into(self) -> Transport {
        Transport(self.0, self.1)
    }
}

impl From<Transport> for (Box<dyn ReadDebugSend>, Box<dyn WriteDebugSend>) {
    fn from(t: Transport) -> Self {
        (t.0, t.1)
    }
}

// A Transport struct encapsulates types that already have the Send trait so it
// is safe to send them across thread boundaries.
unsafe impl Send for Transport {}

fn tcpstream_to_transport(stream: TcpStream) -> Result<Transport> {
    let write = stream.try_clone().map_err(Error::Clone)?;
    Ok(Transport(Box::new(stream), Box::new(write)))
}

fn vsockstream_to_transport(stream: VsockStream) -> Result<Transport> {
    let write = stream.try_clone().map_err(Error::Clone)?;
    Ok(Transport(Box::new(stream), Box::new(write)))
}

/// Abstracts transport methods that accept incoming connections.
pub trait ServerTransport {
    fn accept(&mut self) -> Result<Transport>;
}

/// Abstracts transport methods that initiate incoming connections.
pub trait ClientTransport {
    fn connect(&mut self) -> Result<Transport>;
}

pub const LOOPBACK_DEFAULT: &str = "127.0.0.1:5552";

/// A transport method that listens for incoming IP connections.
pub struct IPServerTransport(TcpListener);

impl IPServerTransport {
    /// `addr` - The address to bind to.
    pub fn new<T: ToSocketAddrs>(addr: T) -> Result<Self> {
        let listener = TcpListener::bind(addr).map_err(Error::Bind)?;
        Ok(IPServerTransport(listener))
    }

    pub fn local_addr(&self) -> Result<SocketAddr> {
        self.0.local_addr().map_err(Error::GetAddress)
    }
}

impl ServerTransport for IPServerTransport {
    fn accept(&mut self) -> Result<Transport> {
        let (stream, _) = handle_eintr!(self.0.accept()).map_err(Error::Accept)?;
        tcpstream_to_transport(stream)
    }
}

/// A transport method that connects over IP.
pub struct IPClientTransport(SocketAddr);

impl IPClientTransport {
    pub fn new<T: ToSocketAddrs>(addr: T) -> Result<Self> {
        let mut iter = addr
            .to_socket_addrs()
            .map_err(|e| Error::ParseAddress(Some(e)))?;
        match iter.next() {
            Some(a) => Ok(IPClientTransport(a)),
            None => Err(Error::ParseAddress(None)),
        }
    }
}

impl ClientTransport for IPClientTransport {
    fn connect(&mut self) -> Result<Transport> {
        let stream = handle_eintr!(TcpStream::connect(&self.0)).map_err(Error::Connect)?;
        tcpstream_to_transport(stream)
    }
}

/// A transport method that listens for incoming vsock connections.
pub struct VsockServerTransport(VsockListener);

impl VsockServerTransport {
    pub fn new() -> Result<Self> {
        let listener = VsockListener::bind(DEFAULT_PORT).map_err(Error::Bind)?;
        Ok(VsockServerTransport(listener))
    }
}

impl ServerTransport for VsockServerTransport {
    fn accept(&mut self) -> Result<Transport> {
        let (stream, _) = handle_eintr!(self.0.accept()).map_err(Error::Accept)?;
        vsockstream_to_transport(stream)
    }
}

/// A transport method that connects over vsock.
pub struct VsockClientTransport(VsockCid);

impl VsockClientTransport {
    pub fn new(cid: VsockCid) -> Result<Self> {
        Ok(VsockClientTransport(cid))
    }
}

impl ClientTransport for VsockClientTransport {
    fn connect(&mut self) -> Result<Transport> {
        let addr = vsock::SocketAddr {
            cid: self.0.into(),
            port: DEFAULT_PORT,
        };
        let stream = handle_eintr!(VsockStream::connect(&addr)).map_err(Error::Connect)?;
        vsockstream_to_transport(stream)
    }
}

#[derive(Debug)]
enum PipeTransportState {
    ServerReady(Transport),
    ClientReady(Transport),
    Either,
}

impl Default for PipeTransportState {
    fn default() -> Self {
        PipeTransportState::Either
    }
}

impl PartialEq for PipeTransportState {
    fn eq(&self, other: &Self) -> bool {
        match &self {
            PipeTransportState::ServerReady(_) => {
                if let PipeTransportState::ServerReady(_) = other {
                    true
                } else {
                    false
                }
            }
            PipeTransportState::ClientReady(_) => {
                if let PipeTransportState::ClientReady(_) = other {
                    true
                } else {
                    false
                }
            }
            PipeTransportState::Either => {
                if let PipeTransportState::Either = other {
                    true
                } else {
                    false
                }
            }
        }
    }
}

// Returns two `Transport` structs connected to each other.
fn create_transport_from_pipes() -> Result<(Transport, Transport)> {
    let (r1, w1) = pipe(true).map_err(Error::Pipe)?;
    let (r2, w2) = pipe(true).map_err(Error::Pipe)?;
    Ok((
        Transport(Box::new(r1), Box::new(w2)),
        Transport(Box::new(r2), Box::new(w1)),
    ))
}

/// A transport method which provides both the server and client abstractions.
///
/// NOTE this only works in process, and is intended for testing.
///
/// It works by generating pairs of pipes which serve as the send and receive
/// sides of both the server and client side Transport. For each call to
/// `accept()` there should be a corresponding call to `connect()` or an error
/// will be returned unless `close()` is called first.
#[derive(Debug, Default)]
pub struct PipeTransport {
    state: PipeTransportState,
}

impl PipeTransport {
    pub fn new() -> Self {
        PipeTransport {
            state: PipeTransportState::Either,
        }
    }

    pub fn close(&mut self) {
        self.state = PipeTransportState::Either;
    }
}

impl ServerTransport for PipeTransport {
    fn accept(&mut self) -> Result<Transport> {
        match replace(&mut self.state, PipeTransportState::Either) {
            PipeTransportState::ServerReady(t) => Ok(t),
            PipeTransportState::ClientReady(t) => {
                self.state = PipeTransportState::ClientReady(t);
                Err(Error::InvalidState)
            }
            PipeTransportState::Either => {
                let (t1, t2) = create_transport_from_pipes()?;
                self.state = PipeTransportState::ClientReady(t1);
                Ok(t2)
            }
        }
    }
}

impl ClientTransport for PipeTransport {
    fn connect(&mut self) -> Result<Transport> {
        match replace(&mut self.state, PipeTransportState::Either) {
            PipeTransportState::ServerReady(t) => {
                self.state = PipeTransportState::ServerReady(t);
                Err(Error::InvalidState)
            }
            PipeTransportState::ClientReady(t) => Ok(t),
            PipeTransportState::Either => {
                let (t1, t2) = create_transport_from_pipes()?;
                self.state = PipeTransportState::ServerReady(t1);
                Ok(t2)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::thread::spawn;

    const CLIENT_SEND: [u8; 7] = [1, 2, 3, 4, 5, 6, 7];
    const SERVER_SEND: [u8; 5] = [11, 12, 13, 14, 15];

    fn get_ip_transport() -> Result<(IPServerTransport, IPClientTransport)> {
        const BIND_ADDRESS: &str = "127.0.0.1:0";
        let server = IPServerTransport::new(BIND_ADDRESS)?;
        let client = IPClientTransport::new(&server.local_addr()?)?;
        Ok((server, client))
    }

    fn test_transport<S: ServerTransport, C: ClientTransport + Send + 'static>(
        mut server: S,
        mut client: C,
    ) {
        spawn(move || {
            let (mut r, mut w) = client.connect().unwrap().into();
            assert_eq!(w.write(&CLIENT_SEND).unwrap(), CLIENT_SEND.len());

            let mut buf: [u8; SERVER_SEND.len()] = [0; SERVER_SEND.len()];
            r.read_exact(&mut buf).unwrap();
            assert_eq!(buf, SERVER_SEND);
        });

        let (mut r, mut w) = server.accept().unwrap().into();
        assert_eq!(w.write(&SERVER_SEND).unwrap(), SERVER_SEND.len());

        let mut buf: [u8; CLIENT_SEND.len()] = [0; CLIENT_SEND.len()];
        r.read_exact(&mut buf).unwrap();
        assert_eq!(buf, CLIENT_SEND);
    }

    #[test]
    fn iptransport_new() {
        let _ = get_ip_transport().unwrap();
    }

    #[test]
    fn iptransport() {
        let (server, client) = get_ip_transport().unwrap().into();
        test_transport(server, client);
    }

    // TODO modify this to be work with concurrent vsock usage.
    #[test]
    fn vsocktransport() {
        let server = VsockServerTransport::new().unwrap();
        let client = VsockClientTransport::new(VsockCid::Local).unwrap();
        test_transport(server, client);
    }

    #[test]
    fn pipetransport_new() {
        let p = PipeTransport::new();
        assert_eq!(p.state, PipeTransportState::Either);
    }

    #[test]
    fn pipetransport_close() {
        let (t1, t2) = create_transport_from_pipes().unwrap();
        for a in [
            PipeTransportState::Either,
            PipeTransportState::ClientReady(t1),
            PipeTransportState::ServerReady(t2),
        ]
        .iter_mut()
        {
            let mut p = PipeTransport {
                state: replace(a, PipeTransportState::Either),
            };
            p.close();
            assert_eq!(p.state, PipeTransportState::Either);
        }
    }

    #[test]
    fn pipetransport() {
        let mut p = PipeTransport::new();

        let client = p.connect().unwrap();
        spawn(move || {
            let (mut r, mut w) = client.into();
            assert_eq!(w.write(&CLIENT_SEND).unwrap(), CLIENT_SEND.len());

            let mut buf: [u8; SERVER_SEND.len()] = [0; SERVER_SEND.len()];
            r.read_exact(&mut buf).unwrap();
            assert_eq!(buf, SERVER_SEND);
        });

        let (mut r, mut w) = p.accept().unwrap().into();
        assert_eq!(w.write(&SERVER_SEND).unwrap(), SERVER_SEND.len());

        let mut buf: [u8; CLIENT_SEND.len()] = [0; CLIENT_SEND.len()];
        r.read_exact(&mut buf).unwrap();
        assert_eq!(buf, CLIENT_SEND);
    }
}
