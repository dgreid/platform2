// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A generic RPC implementation that depends on serializable and deserializable enums for requests
//! and responses.

use std::os::unix::io::{AsRawFd, RawFd};
use std::result::Result as StdResult;

use serde::de::DeserializeOwned;
use serde::Serialize;
use sys_util::{self, error};
use thiserror::Error as ThisError;

use crate::communication::{self, read_message, write_message};
use crate::linux::events::{
    AddEventSourceMutator, Error as EventsError, EventMultiplexer, EventSource, Mutator,
    RemoveFdMutator,
};
use crate::transport::{self, ServerTransport, Transport, TransportType};
use std::convert::TryInto;

#[derive(ThisError, Debug)]
pub enum Error {
    #[error("failed to create the transport: {0:?}")]
    NewTransport(transport::Error),
    // This is used by sirenia-rpc-macros
    #[error("communication failed: {0:?}")]
    Communication(communication::Error),
    #[error("got the wrong response")]
    ResponseMismatch,
}

type Result<T> = StdResult<T, Error>;

/// Registers an RPC server bound based on the specified TransportType that executes the given
/// handler for each received message. If custom logic is needed to filter incoming connections,
/// use TransportServer with an implementation of ConnectionHandler.
pub fn register_server<H: MessageHandler + 'static>(
    event_multiplexer: &mut EventMultiplexer,
    transport: &TransportType,
    handler: H,
) -> StdResult<(), EventsError> {
    let boxed: Box<dyn EventSource> = Box::new(
        TransportServer::new(&transport, SingleRpcConnectionHandler::new(handler)).unwrap(),
    );
    event_multiplexer.add_event(boxed)
}

/// A paring between request and response messages. Define this trait for the desired
/// Request-Response pair to make Invoker<P> available for Transport instances.
pub trait Procedure {
    type Request: Serialize + DeserializeOwned;
    type Response: Serialize + DeserializeOwned;
}

/// The server-side implementation of a Procedure. Define this trait for use with RpcDispatcher.
pub trait MessageHandler: Procedure + Clone {
    fn handle_message(&self, request: Self::Request) -> StdResult<Self::Response, ()>;
}

/// The client-side of an RPC. Note that this is implemented genericly for Transport.
pub trait Invoker<P: Procedure> {
    fn invoke(&mut self, request: P::Request) -> StdResult<P::Response, communication::Error>;
}

impl<P: Procedure> Invoker<P> for Transport {
    fn invoke(&mut self, request: P::Request) -> StdResult<P::Response, communication::Error> {
        write_message(&mut self.w, request)?;
        read_message(&mut self.r)
    }
}

/// A handler for incoming TransportServer connections.
pub trait ConnectionHandler {
    fn handle_incoming_connection(&mut self, connection: Transport) -> Option<Box<dyn Mutator>>;
}

/// Manages a single RPC connection. Use this in cases where a
pub struct RpcDispatcher<H: MessageHandler + 'static> {
    handler: H,
    transport: Transport,
}

impl<H: MessageHandler + 'static> RpcDispatcher<H> {
    pub fn new(handler: H, transport: Transport) -> Self {
        RpcDispatcher { handler, transport }
    }
}

impl<H: MessageHandler + 'static> AsRawFd for RpcDispatcher<H> {
    fn as_raw_fd(&self) -> RawFd {
        self.transport.as_raw_fd()
    }
}

/// Reads RPC messages from the transport and dispatches them to RPC handler.
impl<H: MessageHandler + 'static> EventSource for RpcDispatcher<H> {
    fn on_event(&mut self) -> StdResult<Option<Box<dyn Mutator>>, String> {
        //TODO: Fix this. It is a DoS risk because it is a blocking read on an epoll.
        Ok(match read_message(&mut self.transport.r) {
            Ok(Option::<H::Request>::Some(r)) => {
                if let Ok(response) = self.handler.handle_message(r) {
                    match write_message(&mut self.transport.w, response) {
                        Ok(()) => None,
                        _ => Some(()),
                    }
                } else {
                    Some(())
                }
            }
            Ok(Option::<H::Request>::None) => {
                error!("RpcHandler got empty message");
                Some(())
            }
            Err(e) => {
                error!("RpcHandler error: {:?}", e);
                Some(())
            }
        }
        // Some errors result in a transport that is no longer valid and should be removed. Rather
        // than creating the removal mutator in each case, map () to the removal mutator.
        .map(|()| -> Box<dyn Mutator> { Box::new(RemoveFdMutator(self.transport.as_raw_fd())) }))
    }
}

pub struct SingleRpcConnectionHandler<H: MessageHandler + 'static> {
    handler: H,
}

impl<H: MessageHandler + 'static> SingleRpcConnectionHandler<H> {
    pub fn new(handler: H) -> Self {
        SingleRpcConnectionHandler { handler }
    }
}

impl<H: MessageHandler + 'static> ConnectionHandler for SingleRpcConnectionHandler<H> {
    fn handle_incoming_connection(&mut self, connection: Transport) -> Option<Box<dyn Mutator>> {
        Some(Box::new(AddEventSourceMutator(Some(Box::new(
            RpcDispatcher::new(self.handler.clone(), connection),
        )))))
    }
}

/// Listens for incoming connections and sets up a RPCHandler for each.
pub struct TransportServer<H: ConnectionHandler> {
    handler: H,
    transport: Box<dyn ServerTransport>,
}

impl<H: ConnectionHandler> TransportServer<H> {
    pub fn new(bind_addr: &TransportType, handler: H) -> Result<Self> {
        Ok(TransportServer {
            handler,
            transport: bind_addr.try_into().map_err(Error::NewTransport)?,
        })
    }
}

impl<H: ConnectionHandler> AsRawFd for TransportServer<H> {
    fn as_raw_fd(&self) -> RawFd {
        self.transport.as_raw_fd()
    }
}

/// Creates a EventSource that adds any accept connections and returns a Mutator that will add the
/// client connection to the EventMultiplexer when applied.
impl<H: ConnectionHandler> EventSource for TransportServer<H> {
    fn on_event(&mut self) -> StdResult<Option<Box<dyn Mutator>>, String> {
        Ok(match self.transport.accept() {
            Ok(t) => self.handler.handle_incoming_connection(t),
            Err(e) => {
                error!("RpcServer error: {:?}", e);
                Some(Box::new(RemoveFdMutator(self.as_raw_fd())))
            }
        })
    }
}

#[cfg(test)]
mod test {
    use super::*;

    use std::i32;
    use std::thread::spawn;

    use serde::Deserialize;

    use crate::transport::{self, ClientTransport, IPClientTransport, IPServerTransport};

    #[derive(Serialize, Deserialize)]
    enum Request {
        CheckedNeg(i32),
        CheckedAdd(i32, i32),
        Terminate,
    }

    #[derive(Serialize, Deserialize)]
    enum Response {
        CheckedNeg(Option<i32>),
        CheckedAdd(Option<i32>),
    }

    #[derive(Clone)]
    struct TestHandler {}

    impl Procedure for TestHandler {
        type Request = Request;
        type Response = Response;
    }

    impl MessageHandler for TestHandler {
        fn handle_message(&self, request: Self::Request) -> StdResult<Self::Response, ()> {
            match request {
                Request::CheckedNeg(v) => Ok(Response::CheckedNeg(v.checked_neg())),
                Request::CheckedAdd(a, b) => Ok(Response::CheckedAdd(a.checked_add(b))),
                Request::Terminate => Err(()),
            }
        }
    }

    fn get_ip_transport() -> StdResult<(IPServerTransport, IPClientTransport), transport::Error> {
        const BIND_ADDRESS: &str = "127.0.0.1:0";
        let server = IPServerTransport::new(BIND_ADDRESS)?;
        // Bind to an ephemeral port (denoted by port 0).
        let client = IPClientTransport::new(&server.local_addr()?, 0)?;
        Ok((server, client))
    }

    #[test]
    fn smoke_test() {
        let (server_transport, mut client_transport) = get_ip_transport().unwrap();

        let mut ctx = EventMultiplexer::new().unwrap();

        // This would use register_server(ctx, &server_transport, TestHandler{}), but for testing
        // this sets up an IP connection a head of time on an ephemeral port to avoid port
        // conflicts.
        let boxed: Box<dyn EventSource> = Box::new(TransportServer {
            transport: Box::new(server_transport),
            handler: SingleRpcConnectionHandler::new(TestHandler {}),
        });
        ctx.add_event(boxed).unwrap();

        let handle = spawn(move || {
            // Queue the client RPC:
            let mut connection = client_transport.connect().unwrap();
            let neg_resp =
                Invoker::<TestHandler>::invoke(&mut connection, Request::CheckedNeg(125)).unwrap();
            assert!(matches!(neg_resp, Response::CheckedNeg(Some(-125))));

            let add_resp =
                Invoker::<TestHandler>::invoke(&mut connection, Request::CheckedAdd(5, 4)).unwrap();
            assert!(matches!(add_resp, Response::CheckedAdd(Some(9))));

            assert!(Invoker::<TestHandler>::invoke(&mut connection, Request::Terminate).is_err());
        });

        // Handle initial connection.
        ctx.run_once().unwrap();

        // Run until Request::Terminate which should remove the client connection.
        while ctx.len() > 1 {
            ctx.run_once().unwrap();
        }
        handle.join().unwrap();
    }
}
