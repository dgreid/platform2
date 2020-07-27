// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt;
use std::io::{self, BufRead, BufReader, BufWriter, Read, Write};
use std::num::ParseIntError;
use std::str::FromStr;

use sys_util::{debug, error};
use tiny_http::{Header, Method};

use crate::io_adapters::{ChunkedWriter, CompleteReader, LoggingReader};
use crate::usb_connector::UsbConnection;
use crate::util::read_until_delimiter;

#[derive(Debug)]
pub enum Error {
    DuplicateBodyReader,
    EmptyField(String),
    ForwardRequestBody(io::Error),
    MalformedRequest,
    MalformedContentLength(String, ParseIntError),
    ParseResponse(httparse::Error),
    ReadResponseHeader(io::Error),
    WriteRequestHeader(io::Error),
    WriteResponse(io::Error),
}

impl std::error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            DuplicateBodyReader => write!(f, "Attempted to call body_reader() multiple times."),
            EmptyField(field) => write!(f, "HTTP Response field {} was unexpectedly empty", field),
            ForwardRequestBody(err) => write!(f, "Forwarding request body failed: {}", err),
            MalformedRequest => write!(f, "HTTP request is malformed"),
            MalformedContentLength(value, err) => write!(
                f,
                "Failed to parse response Content-Length '{}': {}",
                value, err
            ),
            ParseResponse(err) => write!(f, "Failed to parse HTTP Response header: {}", err),
            ReadResponseHeader(err) => write!(f, "Reading response header failed: {}", err),
            WriteRequestHeader(err) => write!(f, "Writing request header failed: {}", err),
            WriteResponse(err) => write!(f, "Responding to request failed: {}", err),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

#[derive(Copy, Clone)]
enum BodyLength {
    Chunked,
    Exactly(u64),
}

struct ResponseReader<R: BufRead + Sized> {
    reader: R,
    body_length: BodyLength,
    header_was_read: bool,
    created_body_reader: bool,
}

impl<R> ResponseReader<R>
where
    R: BufRead + Sized,
{
    fn new(reader: R) -> ResponseReader<R> {
        ResponseReader {
            reader,
            // Assume body is empty unless we see a header to the contrary.
            body_length: BodyLength::Exactly(0),
            header_was_read: false,
            created_body_reader: false,
        }
    }

    fn read_header(&mut self) -> Result<(tiny_http::StatusCode, Vec<Header>)> {
        self.header_was_read = true;

        let buf = read_until_delimiter(&mut self.reader, b"\r\n\r\n")
            .map_err(Error::ReadResponseHeader)?;
        let mut headers = [httparse::EMPTY_HEADER; 32];
        let mut response = httparse::Response::new(&mut headers);
        let (status, headers) = match response.parse(&buf).map_err(Error::ParseResponse)? {
            httparse::Status::Complete(i) if i == buf.len() => {
                let code = response
                    .code
                    .ok_or_else(|| Error::EmptyField("code".to_owned()))?;
                let status = tiny_http::StatusCode::from(code);
                let version = response
                    .version
                    .ok_or_else(|| Error::EmptyField("version".to_owned()))?;
                debug!(
                    "> HTTP/1.{} {} {}",
                    version,
                    code,
                    status.default_reason_phrase()
                );
                let mut parsed_headers = Vec::new();
                for header in headers.iter().take_while(|&&h| h != httparse::EMPTY_HEADER) {
                    if let Ok(h) = Header::from_bytes(header.name, header.value) {
                        parsed_headers.push(h);
                    } else {
                        error!(
                            "Ignoring malformed header {}:{:#?}",
                            header.name, header.value
                        );
                    }
                }
                (status, parsed_headers)
            }
            _ => return Err(Error::MalformedRequest),
        };

        // Determine the size of the body content.
        for header in headers.iter() {
            if header.field.equiv("Content-Length") {
                let length = u64::from_str(header.value.as_str()).map_err(|e| {
                    Error::MalformedContentLength(header.value.as_str().to_string(), e)
                })?;
                self.body_length = BodyLength::Exactly(length);
                break;
            }

            if header.field.equiv("Transfer-Encoding") {
                self.body_length = BodyLength::Chunked;
                break;
            }
        }

        Ok((status, headers))
    }

    fn body_reader<'r>(&'r mut self) -> Result<Box<dyn Read + 'r>> {
        if self.created_body_reader {
            return Err(Error::DuplicateBodyReader);
        }

        self.created_body_reader = true;
        match self.body_length {
            BodyLength::Exactly(length) => {
                let reader = (&mut self.reader).take(length);
                Ok(Box::new(CompleteReader::new(reader)))
            }
            BodyLength::Chunked => {
                let reader = chunked_transfer::Decoder::new(&mut self.reader);
                Ok(Box::new(CompleteReader::new(reader)))
            }
        }
    }
}

impl<R> Drop for ResponseReader<R>
where
    R: BufRead,
{
    fn drop(&mut self) {
        if !self.created_body_reader {
            debug!("Draining in drop");
            if !self.header_was_read {
                // Read header to figure out how long the body is.
                let _ = self.read_header();
            }

            // Create a body reader which will totally read the response on drop.
            let _ = self.body_reader();
        }
    }
}

fn is_end_to_end(header: &Header) -> bool {
    match header.field.as_str().as_str() {
        "Connection"
        | "Expect" // Technically end-to-end, but we want to filter it.
        | "Keep-Alive"
        | "Proxy-Authenticate"
        | "Proxy-Authorization"
        | "TE"
        | "Trailers"
        | "Transfer-Encoding"
        | "Upgrade" => false,
        _ => true,
    }
}

fn supports_request_body(method: &Method) -> bool {
    match method {
        Method::Get | Method::Head | Method::Delete | Method::Options | Method::Trace => false,
        _ => true,
    }
}

fn serialize_request_header(request: &tiny_http::Request, chunked: bool) -> String {
    let mut serialized_header = format!("{} {} HTTP/1.1\r\n", request.method(), request.url());
    let mut have_content_length = false;
    for header in request.headers().iter().filter(|&h| is_end_to_end(h)) {
        if header.field.as_str() == "Content-Length" {
            have_content_length = true;
            // Do not add the content_length header if we're going to be using
            // a chunked encoding.
            if chunked {
                continue;
            }
        }

        let formatted = format!("{}: {}\r\n", header.field, header.value);
        serialized_header += &formatted;
    }

    if chunked {
        serialized_header += "Transfer-Encoding: chunked\r\n";
    } else if !have_content_length {
        serialized_header += "Content-Length: 0\r\n";
    }

    serialized_header += "\r\n";

    serialized_header
}

pub fn handle_request(usb: UsbConnection, mut request: tiny_http::Request) -> Result<()> {
    debug!(
        "< {} {} HTTP/1.{}",
        request.method(),
        request.url(),
        request.http_version().1
    );

    // If we aren't explicitly given a Content-Length: 0 header, and the method
    // permits a request body, there could be body content, so we should switch
    // to a chunked transfer.
    let has_body = supports_request_body(request.method()) && request.body_length() != Some(0);

    // Write the modified request header to the printer.
    let header = serialize_request_header(&request, has_body);
    let mut usb_writer = BufWriter::new(&usb);
    usb_writer
        .write(header.as_bytes())
        .map_err(Error::WriteRequestHeader)?;
    usb_writer.flush().map_err(Error::WriteRequestHeader)?;

    // Now that we have written data to the printer, we must ensure that we read
    // a complete HTTP response from the printer. Otherwise, that data may
    // remain in the printer's buffers and be sent to some other client.
    // ResponseReader ensures that this happens internally.
    let usb_reader = BufReader::new(&usb);
    let mut response_reader = ResponseReader::new(usb_reader);

    if has_body {
        debug!("* Forwarding client request body");
        let mut logging_reader = LoggingReader::new(request.as_reader(), "client".to_string());
        let mut chunked_writer = ChunkedWriter::new(usb_writer);
        io::copy(&mut logging_reader, &mut chunked_writer).map_err(Error::ForwardRequestBody)?;
        chunked_writer.flush().map_err(Error::ForwardRequestBody)?;
    }

    debug!("* Reading printer response header");
    let (status, headers) = response_reader.read_header()?;

    debug!("* Forwarding printer response body");
    let body_reader = response_reader.body_reader()?;
    let response = tiny_http::Response::new(status, headers, body_reader, None, None);
    request.respond(response).map_err(Error::WriteResponse)?;

    debug!("* Finished processing request");
    Ok(())
}
