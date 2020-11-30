## ManaTEE Runtime

This library is the API endpoint for TEE applications to communicate with
Trichechus.

### Storage APIs

The main feature provided by this library is storage capabilities. This library
offers abstractions for reading and writing data via scoped data, a key value
store, or raw APIs.

#### Raw APIs

This includes `read_raw` and `write_raw` that can be used to read and write
data to the ManaTEE backing store.

#### ScopedData

This reads the data into a local store on construction and writes it back on
flush or drop.

#### Scoped Key Value API

This reads in an entire key value store on construction and writes it back on
flush or drop.
