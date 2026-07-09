# bare-tcp-cat

A native [Bare](https://github.com/nicolo-ribaudo/bare) addon that fetches data from an IP address over TCP and returns the result as a JavaScript `Buffer`. The native layer uses [libuv](https://docs.libuv.org/en/v1.x/tcp.html) for all async I/O.

## API

```js
const tcpCat = require('./index')

const response = await tcpCat('1.1.1.1', 80, 'GET / HTTP/1.1\r\nHost: cloudflare.com\r\nConnection: close\r\n\r\n')
console.log(response) // <Buffer 48 54 54 50 2f 31 2e 31 ...>
console.log(response.toString()) // HTTP/1.1 301 Moved Permanently ...
```

## Building

Install the build tool globally if you haven't already:

```sh
npm install -g bare-make
```

Install dependencies:

```sh
npm install
```

Generate the CMake build system (only needed once per checkout):

```sh
bare-make generate --debug
```

Compile the native binding and link it into `prebuilds/`:

```sh
bare-make build
bare-make install --link
```

## Testing

Tests run using [brittle](https://npmjs.com/brittle) under the Bare runtime.
The test suite spins up a local TCP server so no internet connection is required.

```sh
bare test.js
```

Expected output:

```
TAP version 13

# basic tcpCat request and response
ok 1 - should be equal
ok 2 - should be equal
ok 1 - basic tcpCat request and response # time=10ms

1..1
# tests 1/1 pass
# asserts 2/2 pass
# ok
```

## Design Notes

### What lives in C (`binding.c`)

- TCP socket lifecycle via `uv_tcp_init`, `uv_tcp_connect`, `uv_write`, `uv_read_start`, `uv_close`
- Per-request context struct (`tcp_request_t`) allocated on the heap — one per concurrent call, no global state
- Dynamic response buffer accumulated across multiple `uv_read_cb` invocations until `UV_EOF`
- JS Promise created with `js_create_promise`, resolved with a `js_create_arraybuffer` on `UV_EOF`, rejected with `js_create_error` on any failure
- `js_open_handle_scope` / `js_close_handle_scope` wrapped around all async JS value creation inside libuv callbacks (required — V8 has no automatic scope in async C callbacks)
- Memory freed only inside `on_close`, which libuv calls after the handle is fully closed

### What lives in JavaScript (`index.js`)

- Argument validation and the public `tcpCat(ip, port, message)` signature
- Wrapping the resolved `ArrayBuffer` into a `Buffer` object via `Buffer.from(arrayBuffer)`

### Key design decision: no global state

Each call to `tcpCat` allocates its own `tcp_request_t` and attaches it to `socket.data`. This makes concurrent calls safe — each request owns its own socket, buffer, and promise deferred independently.

## AI Usage Disclosure

I used an AI assistant (Gemini/Antigravity) during this exercise for the following:

- **Looking up `libjs` API signatures** — specifically `js_create_promise`, `js_resolve_deferred`, `js_reject_deferred`, `js_get_env_loop`, `js_open_handle_scope`, and `js_create_arraybuffer` from [`holepunchto/libjs/include/js.h`](https://github.com/holepunchto/libjs/blob/main/include/js.h), since I was not familiar with Bare's `js_*` prefix

The following was done without AI assistance:
- The libuv TCP flow (connect → write → read → close)
- The per-request context struct design and concurrency safety reasoning
- The test design using a local mock server and dynamic port allocation
- Debugging the `bare-net` module resolution issue under the Bare runtime

## License

Apache-2.0
