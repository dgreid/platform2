// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles the transport abstractions for Sirenia. This allows communication
//! between Dugong and Trichechus to be tested locally without needing to use
//! vsock, or even IP sockets (if pipes are used). It also allows for
//! implementing communication for cases were vsock isn't available or
//! appropriate.

use std::boxed::Box;
use std::convert::TryInto;
use std::fmt::{self, Debug, Display};
use std::fs::File;
use std::io::{self, Read, Write};
use std::iter::Iterator;
use std::marker::Send;
use std::net::{
    Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4, SocketAddrV6, TcpListener, TcpStream,
    ToSocketAddrs,
};
use std::os::raw::c_uint;
use std::os::unix::io::{AsRawFd, FromRawFd, IntoRawFd, RawFd};
use std::str::FromStr;

use core::mem::replace;
use libchromeos::net::{InetVersion, TcpSocket};
use libchromeos::vsock::{
    AddrParseError, SocketAddr as VSocketAddr, ToSocketAddr, VsockCid, VsockListener, VsockSocket,
    VsockStream, VMADDR_PORT_ANY,
};
use sys_util::{handle_eintr, pipe};

pub const DEFAULT_SERVER_PORT: u32 = 5552;
pub const DEFAULT_CLIENT_PORT: u32 = 5553;
pub const DEFAULT_CONNECTION_R_FD: i32 = 555;
pub const DEFAULT_CONNECTION_W_FD: i32 = 556;
pub const CROS_CONNECTION_R_FD: i32 = 0;
pub const CROS_CONNECTION_W_FD: i32 = 1;
pub const CROS_CONNECTION_ERR_FD: i32 = 2;

#[derive(Debug)]
pub enum Error {
    /// Failed to parse a socket address.
    SocketAddrParse(Option<io::Error>),
    /// Failed to parse a vsock socket address.
    VSocketAddrParse(AddrParseError),
    /// Got an unknown transport type.
    UnknownTransportType,
    /// Failed to parse URI.
    URIParse,
    /// Failed to clone a fd.
    Clone(io::Error),
    /// Error creating a socket.
    Socket(io::Error),
    /// Failed to bind a socket.
    Bind(io::Error),
    /// Failed to get the socket address.
    GetAddress(io::Error),
    /// Failed to accept the incoming connection.
    Accept(io::Error),
    /// Failed to connect to the socket address.
    Connect(io::Error),
    /// Failed to obtain the local port.
    LocalAddr(io::Error),
    /// Failed to construct the pipe.
    Pipe(sys_util::Error),
    /// The pipe transport was in the wrong state to complete the requested operation.
    InvalidState,
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use self::Error::*;

        match self {
            SocketAddrParse(e) => match e {
                Some(i) => write!(f, "failed to parse the socket address: {}", i),
                None => write!(f, "failed to parse the socket address"),
            },
            VSocketAddrParse(_) => write!(f, "failed to parse the vsock socket address"),
            UnknownTransportType => write!(f, "got an unrecognized transport type"),
            URIParse => write!(f, "failed to parse the URI"),
            Clone(e) => write!(f, "failed to clone fd: {}", e),
            Socket(e) => write!(f, "failed to create socket: {}", e),
            Bind(e) => write!(f, "failed to bind: {}", e),
            GetAddress(e) => write!(f, "failed to get the socket address: {}", e),
            Accept(e) => write!(f, "failed to accept connection: {}", e),
            Connect(e) => write!(f, "failed to connect: {}", e),
            LocalAddr(e) => write!(f, "failed to get port: {}", e),
            Pipe(e) => write!(f, "failed to construct the pipe: {}", e),
            InvalidState => write!(f, "pipe transport was in the wrong state"),
        }
    }
}

/// The result of an operation in this crate.
pub type Result<T> = std::result::Result<T, Error>;

/// An abstraction wrapper to support the receiving side of a transport method.
pub trait TransportRead: Read + Debug + Send + AsRawFd {
    fn into_raw_fd(self: Box<Self>) -> RawFd;
}
impl<T: Read + Debug + Send + AsRawFd + IntoRawFd> TransportRead for T {
    fn into_raw_fd(self: Box<Self>) -> RawFd {
        (*self).into_raw_fd()
    }
}

/// An abstraction wrapper to support the sending side of a transport method.
pub trait TransportWrite: Write + Debug + Send + AsRawFd {
    fn into_raw_fd(self: Box<Self>) -> RawFd;
}
impl<T: Write + Debug + Send + AsRawFd + IntoRawFd> TransportWrite for T {
    fn into_raw_fd(self: Box<Self>) -> RawFd {
        (*self).into_raw_fd()
    }
}

/// Transport options that can be selected or uniquely represent a transport instance.
#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub enum TransportType {
    VsockConnection(VSocketAddr),
    IpConnection(SocketAddr),
    Pipe(RawFd, RawFd),
}

impl TransportType {
    pub fn try_into_client(&self, bind_port: Option<u32>) -> Result<Box<dyn ClientTransport>> {
        match self {
            TransportType::IpConnection(url) => Ok(Box::new(IPClientTransport::new(
                &url,
                bind_port.unwrap_or(0) as u16,
            )?)),
            TransportType::VsockConnection(url) => Ok(Box::new(VsockClientTransport::new(
                &url,
                bind_port.unwrap_or(VMADDR_PORT_ANY),
            )?)),
            _ => Err(Error::UnknownTransportType),
        }
    }

    pub fn get_port(&self) -> Result<u32> {
        match self {
            TransportType::IpConnection(addr) => Ok(addr.port() as u32),
            TransportType::VsockConnection(addr) => Ok(addr.port),
            _ => Err(Error::UnknownTransportType),
        }
    }
}

impl From<VSocketAddr> for TransportType {
    fn from(a: VSocketAddr) -> Self {
        TransportType::VsockConnection(a)
    }
}

impl From<SocketAddr> for TransportType {
    fn from(a: SocketAddr) -> Self {
        TransportType::IpConnection(a)
    }
}

impl From<(RawFd, RawFd)> for TransportType {
    fn from(a: (RawFd, RawFd)) -> Self {
        TransportType::Pipe(a.0, a.1)
    }
}

fn parse_ip_connection(value: &str) -> Result<TransportType> {
    let mut iter = value
        .to_socket_addrs()
        .map_err(|e| Error::SocketAddrParse(Some(e)))?;
    match iter.next() {
        None => Err(Error::SocketAddrParse(None)),
        Some(a) => Ok(TransportType::IpConnection(a)),
    }
}

fn parse_vsock_connection(value: &str) -> Result<TransportType> {
    let socket_addr: VSocketAddr = value.to_socket_addr().map_err(Error::VSocketAddrParse)?;
    Ok(TransportType::VsockConnection(socket_addr))
}

impl FromStr for TransportType {
    type Err = Error;

    fn from_str(value: &str) -> Result<Self> {
        if value.is_empty() {
            return Err(Error::URIParse);
        }
        let parts: Vec<&str> = value.split("://").collect();
        match parts.len() {
            2 => match parts[0] {
                "vsock" | "VSOCK" => parse_vsock_connection(parts[1]),
                "ip" | "IP" => parse_ip_connection(parts[1]),
                _ => Err(Error::UnknownTransportType),
            },
            // TODO: Should this still be the default?
            1 => parse_ip_connection(value),
            _ => Err(Error::URIParse),
        }
    }
}

impl TryInto<Box<dyn ServerTransport>> for &TransportType {
    type Error = Error;

    fn try_into(self) -> Result<Box<dyn ServerTransport>> {
        match self {
            TransportType::IpConnection(url) => Ok(Box::new(IPServerTransport::new(&url)?)),
            TransportType::VsockConnection(url) => Ok(Box::new(VsockServerTransport::new(&url)?)),
            _ => Err(Error::UnknownTransportType),
        }
    }
}

/// Wraps a complete transport method, both sending and receiving.
#[derive(Debug)]
pub struct Transport {
    pub r: Box<dyn TransportRead>,
    pub w: Box<dyn TransportWrite>,
    pub id: TransportType,
}

impl Into<Transport>
    for (
        Box<dyn TransportRead>,
        Box<dyn TransportWrite>,
        TransportType,
    )
{
    fn into(self) -> Transport {
        Transport {
            r: self.0,
            w: self.1,
            id: self.2,
        }
    }
}

impl From<Transport>
    for (
        Box<dyn TransportRead>,
        Box<dyn TransportWrite>,
        TransportType,
    )
{
    fn from(t: Transport) -> Self {
        (t.r, t.w, t.id)
    }
}

/// Returns a RawFd for the read file descriptor for use with EventMultiplexer.
impl AsRawFd for Transport {
    fn as_raw_fd(&self) -> RawFd {
        self.r.as_raw_fd()
    }
}

// A Transport struct encapsulates types that already have the Send trait so it
// is safe to send them across thread boundaries.
unsafe impl Send for Transport {}

fn tcpstream_to_transport(stream: TcpStream, id: SocketAddr) -> Result<Transport> {
    let write = stream.try_clone().map_err(Error::Clone)?;
    Ok(Transport {
        r: Box::new(stream),
        w: Box::new(write),
        id: TransportType::from(id),
    })
}

fn vsockstream_to_transport(stream: VsockStream, id: VSocketAddr) -> Result<Transport> {
    let write = stream.try_clone().map_err(Error::Clone)?;
    Ok(Transport {
        r: Box::new(stream),
        w: Box::new(write),
        id: TransportType::from(id),
    })
}

/// Abstracts transport methods that accept incoming connections.
pub trait ServerTransport: AsRawFd {
    fn accept(&mut self) -> Result<Transport>;
}

/// Abstracts transport methods that initiate incoming connections.
pub trait ClientTransport {
    fn bind(&mut self) -> Result<TransportType>;
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

impl AsRawFd for IPServerTransport {
    fn as_raw_fd(&self) -> RawFd {
        self.0.as_raw_fd()
    }
}

impl ServerTransport for IPServerTransport {
    fn accept(&mut self) -> Result<Transport> {
        let (stream, addr) = handle_eintr!(self.0.accept()).map_err(Error::Accept)?;
        tcpstream_to_transport(stream, addr)
    }
}

/// A transport method that connects over IP.
pub struct IPClientTransport {
    addr: SocketAddr,
    sock: Option<TcpSocket>,
    bind_port: u16,
}

impl IPClientTransport {
    pub fn new<T: ToSocketAddrs>(to_addrs: T, bind_port: u16) -> Result<Self> {
        let addr = to_addrs
            .to_socket_addrs()
            .map_err(|e| Error::SocketAddrParse(Some(e)))?
            .next()
            .ok_or(Error::SocketAddrParse(None))?;

        Ok(IPClientTransport {
            addr,
            sock: None,
            bind_port,
        })
    }
}

impl ClientTransport for IPClientTransport {
    fn bind(&mut self) -> Result<TransportType> {
        let ver = InetVersion::from_sockaddr(&self.addr);
        let mut sock = TcpSocket::new(ver).map_err(Error::Socket)?;
        let bind_addr: SocketAddr = match &self.addr {
            SocketAddr::V4(_) => SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, self.bind_port).into(),
            SocketAddr::V6(_) => {
                SocketAddrV6::new(Ipv6Addr::UNSPECIFIED, self.bind_port, 0, 0).into()
            }
        };
        sock.bind(bind_addr).map_err(Error::Bind)?;

        let port = sock.local_port().map_err(Error::LocalAddr)?;
        self.sock = Some(sock);
        Ok(match self.addr {
            SocketAddr::V4(_) => SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::UNSPECIFIED, port)),
            SocketAddr::V6(_) => {
                SocketAddr::V6(SocketAddrV6::new(Ipv6Addr::UNSPECIFIED, port, 0, 0))
            }
        }
        .into())
    }

    fn connect(&mut self) -> Result<Transport> {
        if self.sock.is_none() {
            self.bind()?;
        }
        let sock = replace(&mut self.sock, None).unwrap();
        // TODO TcpSocket::connect and VsockSocket::connect need to handle EINTR.
        let stream = sock.connect(self.addr).map_err(Error::Connect)?;
        let addr = stream.local_addr().map_err(Error::LocalAddr)?;
        tcpstream_to_transport(stream, addr)
    }
}

/// A transport method that listens for incoming vsock connections.
pub struct VsockServerTransport(VsockListener);

impl VsockServerTransport {
    pub fn new<T: ToSocketAddr>(addr: T) -> Result<Self> {
        let address: VSocketAddr = addr.to_socket_addr().map_err(Error::VSocketAddrParse)?;
        let listener = VsockListener::bind(address).map_err(Error::Bind)?;
        Ok(VsockServerTransport(listener))
    }
}

impl AsRawFd for VsockServerTransport {
    fn as_raw_fd(&self) -> RawFd {
        self.0.as_raw_fd()
    }
}

impl ServerTransport for VsockServerTransport {
    fn accept(&mut self) -> Result<Transport> {
        let (stream, addr) = handle_eintr!(self.0.accept()).map_err(Error::Accept)?;
        vsockstream_to_transport(stream, addr)
    }
}

/// A transport method that connects over vsock.
pub struct VsockClientTransport {
    addr: VSocketAddr,
    sock: Option<VsockSocket>,
    bind_port: u32,
}

impl VsockClientTransport {
    pub fn new<T: ToSocketAddr>(to_addr: T, bind_port: u32) -> Result<Self> {
        let addr: VSocketAddr = to_addr.to_socket_addr().map_err(Error::VSocketAddrParse)?;
        Ok(VsockClientTransport {
            addr,
            sock: None,
            bind_port,
        })
    }
}

impl ClientTransport for VsockClientTransport {
    fn bind(&mut self) -> Result<TransportType> {
        let mut sock = VsockSocket::new().map_err(Error::Socket)?;
        let bind_addr = VSocketAddr {
            cid: VsockCid::Any,
            port: self.bind_port,
        };
        sock.bind(bind_addr).map_err(Error::Bind)?;

        let port = sock.local_port().map_err(Error::LocalAddr)?;
        self.sock = Some(sock);
        Ok(VSocketAddr {
            cid: VsockCid::Any,
            port,
        }
        .into())
    }

    fn connect(&mut self) -> Result<Transport> {
        if self.sock.is_none() {
            self.bind()?;
        }
        let sock = replace(&mut self.sock, None).unwrap();
        // TODO TcpSocket::connect and VsockSocket::connect need to handle EINTR.
        let stream = sock.connect(&self.addr).map_err(Error::Connect)?;
        let addr = VSocketAddr {
            cid: VsockCid::Any,
            port: stream.local_port().map_err(Error::LocalAddr)?,
        };
        vsockstream_to_transport(stream, addr)
    }
}

#[derive(Debug)]
enum PipeTransportState {
    Bound(Transport, Transport),
    ServerReady(Transport),
    ClientReady(Transport),
    UnBound,
}

impl Default for PipeTransportState {
    fn default() -> Self {
        PipeTransportState::UnBound
    }
}

impl PartialEq for PipeTransportState {
    fn eq(&self, other: &Self) -> bool {
        match &self {
            PipeTransportState::Bound(_, _) => matches!(other, PipeTransportState::Bound(_, _)),
            PipeTransportState::ServerReady(_) => {
                matches!(other, PipeTransportState::ServerReady(_))
            }
            PipeTransportState::ClientReady(_) => {
                matches!(other, PipeTransportState::ClientReady(_))
            }
            PipeTransportState::UnBound => matches!(other, PipeTransportState::UnBound),
        }
    }
}

/// # Safety
///
/// Safe if called from an Only. Needs to be guaranteed to be only called once
/// from a process.
pub unsafe fn create_transport_from_default_fds() -> Result<Transport> {
    let (r, w): (RawFd, RawFd) = (DEFAULT_CONNECTION_R_FD, DEFAULT_CONNECTION_W_FD);
    let id = (r, w);
    Ok(Transport {
        r: Box::new(File::from_raw_fd(DEFAULT_CONNECTION_R_FD)),
        w: Box::new(File::from_raw_fd(DEFAULT_CONNECTION_W_FD)),
        id: TransportType::from(id),
    })
}

// Returns two `Transport` structs connected to each other.
pub fn create_transport_from_pipes() -> Result<(Transport, Transport)> {
    let (r1, w1) = pipe(true).map_err(Error::Pipe)?;
    let id1 = (r1.as_raw_fd(), w1.as_raw_fd());
    let (r2, w2) = pipe(true).map_err(Error::Pipe)?;
    let id2 = (r2.as_raw_fd(), w2.as_raw_fd());
    Ok((
        Transport {
            r: Box::new(r1),
            w: Box::new(w2),
            id: TransportType::from(id1),
        },
        Transport {
            r: Box::new(r2),
            w: Box::new(w1),
            id: TransportType::from(id2),
        },
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
    id: Option<(RawFd, RawFd)>,
}

impl PipeTransport {
    pub fn new() -> Self {
        PipeTransport {
            state: PipeTransportState::UnBound,
            id: None,
        }
    }

    pub fn close(&mut self) {
        self.state = PipeTransportState::UnBound;
        self.id = None;
    }
}

impl AsRawFd for PipeTransport {
    fn as_raw_fd(&self) -> RawFd {
        match &self.id {
            Some(id) => id.0,
            None => Result::<RawFd>::Err(Error::InvalidState).unwrap(),
        }
    }
}

impl ServerTransport for PipeTransport {
    fn accept(&mut self) -> Result<Transport> {
        match replace(&mut self.state, PipeTransportState::UnBound) {
            PipeTransportState::Bound(t1, t2) => {
                self.state = PipeTransportState::ClientReady(t1);
                Ok(t2)
            }
            PipeTransportState::ServerReady(t) => Ok(t),
            PipeTransportState::ClientReady(t) => {
                self.state = PipeTransportState::ClientReady(t);
                Err(Error::InvalidState)
            }
            PipeTransportState::UnBound => {
                let (t1, t2) = create_transport_from_pipes()?;
                self.state = PipeTransportState::ClientReady(t1);
                Ok(t2)
            }
        }
    }
}

impl ClientTransport for PipeTransport {
    fn bind(&mut self) -> Result<TransportType> {
        let (t1, t2) = create_transport_from_pipes()?;
        let id = (t1.r.as_raw_fd(), t2.r.as_raw_fd());
        self.state = PipeTransportState::Bound(t1, t2);
        self.id = Some(id);
        Ok(TransportType::from(id))
    }

    fn connect(&mut self) -> Result<Transport> {
        match replace(&mut self.state, PipeTransportState::UnBound) {
            PipeTransportState::Bound(t1, t2) => {
                self.state = PipeTransportState::ServerReady(t2);
                Ok(t1)
            }
            PipeTransportState::ServerReady(t) => {
                self.state = PipeTransportState::ServerReady(t);
                Err(Error::InvalidState)
            }
            PipeTransportState::ClientReady(t) => Ok(t),
            PipeTransportState::UnBound => {
                self.bind()?;
                self.connect()
            }
        }
    }
}

// This code needs to be here to support tests in transport and cli
const IP_ADDR: &str = "1.1.1.1:1234";

pub fn get_test_ip_uri() -> String {
    format!("ip://{}", IP_ADDR)
}

fn get_test_vsock_addr() -> String {
    let cid: c_uint = VsockCid::Local.into();
    format!("vsock:{}:1", cid)
}

pub fn get_test_vsock_uri() -> String {
    format!("vsock://{}", get_test_vsock_addr())
}

#[cfg(test)]
pub mod tests {
    use super::*;

    use libchromeos::vsock::{VsockCid, VMADDR_PORT_ANY};
    use std::net::{IpAddr, Ipv4Addr};
    use std::thread::spawn;

    const CLIENT_SEND: [u8; 7] = [1, 2, 3, 4, 5, 6, 7];
    const SERVER_SEND: [u8; 5] = [11, 12, 13, 14, 15];

    fn get_ip_transport() -> Result<(IPServerTransport, IPClientTransport)> {
        const BIND_ADDRESS: &str = "127.0.0.1:0";
        let server = IPServerTransport::new(BIND_ADDRESS)?;
        // Bind to an ephemeral port (denoted by port 0).
        let client = IPClientTransport::new(&server.local_addr()?, 0)?;
        Ok((server, client))
    }

    fn test_transport<S: ServerTransport, C: ClientTransport + Send + 'static>(
        mut server: S,
        mut client: C,
    ) {
        spawn(move || {
            let (mut r, mut w, _) = client.connect().unwrap().into();
            assert_eq!(w.write(&CLIENT_SEND).unwrap(), CLIENT_SEND.len());

            let mut buf: [u8; SERVER_SEND.len()] = [0; SERVER_SEND.len()];
            r.read_exact(&mut buf).unwrap();
            assert_eq!(buf, SERVER_SEND);
        });

        let (mut r, mut w, _) = server.accept().unwrap().into();
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
        let (server, mut client) = get_ip_transport().unwrap().into();
        client.bind().unwrap();
        test_transport(server, client);
    }

    // TODO modify this to be work with concurrent vsock usage.
    #[test]
    fn vsocktransport() {
        let server = VsockServerTransport::new((VsockCid::Any, DEFAULT_SERVER_PORT)).unwrap();
        let mut client =
            VsockClientTransport::new((VsockCid::Local, DEFAULT_SERVER_PORT), VMADDR_PORT_ANY)
                .unwrap();
        client.bind().unwrap();
        test_transport(server, client);
    }

    #[test]
    fn pipetransport_new() {
        let p = PipeTransport::new();
        assert_eq!(p.state, PipeTransportState::UnBound);
    }

    #[test]
    fn pipetransport_bind() {
        let mut p = PipeTransport::new();
        p.bind().unwrap();
        assert!(matches!(p.state, PipeTransportState::Bound(_, _)));
    }

    #[test]
    fn pipetransport_close() {
        let (t1, t2) = create_transport_from_pipes().unwrap();
        let id = Some((t1.r.as_raw_fd(), t2.r.as_raw_fd()));
        for a in [
            PipeTransportState::UnBound,
            PipeTransportState::ClientReady(t1),
            PipeTransportState::ServerReady(t2),
        ]
        .iter_mut()
        {
            let mut p = PipeTransport {
                state: replace(a, PipeTransportState::UnBound),
                id,
            };
            p.close();
            assert_eq!(p.state, PipeTransportState::UnBound);
        }
    }

    #[test]
    fn pipetransport() {
        let mut p = PipeTransport::new();

        let client = p.connect().unwrap();
        spawn(move || {
            let (mut r, mut w, _) = client.into();
            assert_eq!(w.write(&CLIENT_SEND).unwrap(), CLIENT_SEND.len());

            let mut buf: [u8; SERVER_SEND.len()] = [0; SERVER_SEND.len()];
            r.read_exact(&mut buf).unwrap();
            assert_eq!(buf, SERVER_SEND);
        });

        let (mut r, mut w, _) = p.accept().unwrap().into();
        assert_eq!(w.write(&SERVER_SEND).unwrap(), SERVER_SEND.len());

        let mut buf: [u8; CLIENT_SEND.len()] = [0; CLIENT_SEND.len()];
        r.read_exact(&mut buf).unwrap();
        assert_eq!(buf, CLIENT_SEND);
    }

    #[test]
    fn parse_ip_connection_valid() {
        let exp_socket = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(1, 1, 1, 1)), 1234);
        let exp_result = TransportType::IpConnection(exp_socket);
        let act_result = parse_ip_connection(&IP_ADDR).unwrap();
        assert_eq!(act_result, exp_result);
    }

    #[test]
    fn parse_ip_connection_invalid() {
        let result = parse_ip_connection("foo");
        match &result {
            Err(Error::SocketAddrParse(_)) => (),
            _ => panic!("Got unexpected result: {:?}", &result),
        }
    }

    #[test]
    fn parse_vsock_connection_valid() {
        let exp_result = TransportType::VsockConnection(VSocketAddr {
            cid: VsockCid::Local,
            port: 1,
        });
        let act_result = parse_vsock_connection(&get_test_vsock_addr()).unwrap();
        assert_eq!(act_result, exp_result);
    }

    // Note: should break rn
    #[test]
    fn parse_vsock_connection_invalid() {
        let result = parse_vsock_connection("foo");
        match &result {
            Err(Error::VSocketAddrParse(AddrParseError)) => (),
            _ => panic!("Got unexpected result: {:?}", &result),
        }
    }

    #[test]
    fn parse_connection_empty() {
        let value = "";
        let act_result = TransportType::from_str(&value);
        match &act_result {
            Err(Error::URIParse) => (),
            _ => panic!("Got unexpected result: {:?}", &act_result),
        }
    }

    #[test]
    fn parse_unknown_connection_type_error() {
        let value = "foo://foo";
        let act_result = TransportType::from_str(&value);
        match &act_result {
            Err(Error::UnknownTransportType) => (),
            _ => panic!("Got unexpected result: {:?}", &act_result),
        }
    }

    #[test]
    fn parse_ip_connection_uri_valid() {
        let exp_socket = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(1, 1, 1, 1)), 1234);
        let exp_result = TransportType::IpConnection(exp_socket);
        let value = get_test_ip_uri();
        let act_result = TransportType::from_str(&value).unwrap();
        assert_eq!(act_result, exp_result);
    }

    #[test]
    fn parse_vsock_connection_uri_valid() {
        let exp_result = TransportType::VsockConnection(VSocketAddr {
            cid: VsockCid::Local,
            port: 1,
        });
        let value = get_test_vsock_uri();
        let act_result = TransportType::from_str(&value).unwrap();
        assert_eq!(act_result, exp_result);
    }

    #[test]
    fn parse_ip_connection_implicit_invalid() {
        let value = "foo";
        let act_result = TransportType::from_str(&value);
        match &act_result {
            Err(Error::SocketAddrParse(_)) => (),
            _ => panic!("Got unexpected result: {:?}", &act_result),
        }
    }

    #[test]
    fn parse_ip_connection_implicit_valid() {
        let exp_socket = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(1, 1, 1, 1)), 1234);
        let exp_result = TransportType::IpConnection(exp_socket);
        let value = IP_ADDR;
        let act_result = TransportType::from_str(&value).unwrap();
        assert_eq!(act_result, exp_result);
    }
}
