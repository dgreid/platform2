// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::{self, BufRead};

use sys_util::{error, EventFd};

/// ConnectionTracker is responsible for keeping track of the number of active connections to
/// ippusb_bridge. Whenever the number of clients connected drops to 0 or increases to 1, a
/// notification is sent on `notify` so that the poll loop can wake up and change its polling
/// behavior (i.e. set up a timeout so that it can shut down after 10 seconds of inactivity).
pub struct ConnectionTracker {
    active_connections: usize,
    notify: EventFd,
}

impl ConnectionTracker {
    pub fn new() -> sys_util::Result<Self> {
        let notify = EventFd::new()?;
        Ok(Self {
            active_connections: 0,
            notify,
        })
    }

    pub fn client_connected(&mut self) {
        self.active_connections += 1;
        if self.active_connections == 1 {
            if let Err(e) = self.notify.write(1) {
                error!("Notifying EventFd failed: {}", e);
            }
        }
    }

    pub fn client_disconnected(&mut self) {
        self.active_connections -= 1;
        if self.active_connections == 0 {
            if let Err(e) = self.notify.write(1) {
                error!("Notifying EventFd failed: {}", e);
            }
        }
    }

    pub fn active_connections(&self) -> usize {
        self.active_connections
    }

    pub fn event_fd(&self) -> &EventFd {
        &self.notify
    }
}

/// Read from `reader` until `delimiter` is seen or EOF is reached.
/// Returns read data.
pub fn read_until_delimiter(reader: &mut dyn BufRead, delimiter: &[u8]) -> io::Result<Vec<u8>> {
    let mut result: Vec<u8> = Vec::new();
    loop {
        let buf = match reader.fill_buf() {
            Ok(buf) => buf,
            Err(ref e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(e),
        };

        if buf.is_empty() {
            return Ok(result);
        }

        // First check if our delimiter spans the old buffer and the new buffer.
        for split in 1..delimiter.len() {
            let (first_delimiter, second_delimiter) = delimiter.split_at(split);
            if first_delimiter.len() > result.len() || second_delimiter.len() > buf.len() {
                continue;
            }

            let first = result.get(result.len() - first_delimiter.len()..);
            let second = buf.get(..second_delimiter.len());
            if let (Some(first), Some(second)) = (first, second) {
                if first == first_delimiter && second == second_delimiter {
                    result.extend_from_slice(second);
                    reader.consume(second_delimiter.len());
                    return Ok(result);
                }
            }
        }

        // Then check if our delimiter occurs in the new buffer.
        if let Some(i) = buf
            .windows(delimiter.len())
            .position(|window| window == delimiter)
        {
            result.extend_from_slice(&buf[..i + delimiter.len()]);
            reader.consume(i + delimiter.len());
            return Ok(result);
        }

        // Otherwise just copy the entire buffer into result.
        let consumed = buf.len();
        result.extend_from_slice(&buf);
        reader.consume(consumed);
    }
}

#[cfg(test)]
mod tests {
    use crate::util::read_until_delimiter;
    use std::io::{BufReader, Cursor};

    #[test]
    fn test_read_until_delimiter() {
        let mut source = Cursor::new(&b"abdcdef"[..]);
        let v = read_until_delimiter(&mut source, b"20").unwrap();
        assert_eq!(v, b"abdcdef");

        let mut source = Cursor::new(&b"abdcdef"[..]);
        let v = read_until_delimiter(&mut source, b"de").unwrap();
        assert_eq!(v, b"abdcde");

        let mut source = Cursor::new(&b"abdcdef"[..]);
        let v = read_until_delimiter(&mut source, b"dc").unwrap();
        assert_eq!(v, b"abdc");

        let mut source = Cursor::new(&b"abdcdef"[..]);
        let v = read_until_delimiter(&mut source, b"abd").unwrap();
        assert_eq!(v, b"abd");

        let mut source = BufReader::with_capacity(2, Cursor::new(&b"abdcdeffegh"[..]));
        let v = read_until_delimiter(&mut source, b"bdc").unwrap();
        assert_eq!(v, b"abdc");

        let v = read_until_delimiter(&mut source, b"ef").unwrap();
        assert_eq!(v, b"def");

        let v = read_until_delimiter(&mut source, b"g").unwrap();
        assert_eq!(v, b"feg");
    }
}
