# RytyNet (experimental online mode)

RytyNet is the Ryty 1.0.7 online-mode bridge. It gives the ported game's
network libraries a direct path onto the host's TCP/IP stack instead of the
offline error returns the runtime uses by default. It is **experimental**:
the surface it covers today is plain HTTP/1.1 over TCP plus a signed-in
 PlayStation Network presence, and both are behind an explicit opt-in.

## What it does today

| Area | Behaviour with RytyNet | Behaviour without (default) |
| ---- | ---------------------- | --------------------------- |
| `libSceHttp` | `sceHttpSendRequest` performs a real blocking HTTP/1.1 request over the host's sockets; `sceHttpReadData`, `sceHttpGetStatusCode` and `sceHttpGetAllResponseHeaders` return the response. Chunked transfer responses are decoded. | Every network-touching call fails with the library's network error (`0x80431063`). |
| `libSceNpManager` | `sceNpGetState` reports signed in, `sceNpGetOnlineId`/`sceNpGetNpId` report the local identity `RytyPlayer`, reachability reports available. Nothing talks to Sony's servers; this exists so games that refuse to continue while signed out can reach their menus. | The user is reported as signed out and online queries fail. |
| `libRytyNet` | New runtime module carrying the bridge; exposes `RytyNetIsEnabled`, `RytyNetVersion`, `RytyNetGetStatus` and `RytyNetSetEnabled` (diagnostics). | Not consulted. |

HTTPS is **not** part of the experimental bridge: an `https` request parses
but fails with the network error and logs a `[rytynet]` line explaining that
TLS is unsupported. The non-blocking `sceHttp*Epoll` API also remains offline.

## Enabling it

RytyNet is off unless explicitly enabled. Three ways, in priority order:

1. `RTY_NET_ENABLE=1` in the environment of the ported game
   (`RTY_NET_ENABLE=0` forces it off even if a marker exists).
2. Port the executable with `ryty --rytynet` — the relinker then writes a
   `<output>.rytynet` marker next to the ported executable, and the runtime
   enables the bridge when that marker is present.
3. Diagnostic override via `RytyNetSetEnabled` (see below).

`ryty --rytynet` also records `"rytyNet": true` in the JSON port report and
adds an "experimental" warning, so a port's configuration stays auditable.

## Logging and diagnostics

Set `RTY_NET_LOG=1` to make the bridge log its decisions and every request to
stderr with a `[rytynet]` prefix, for example:

```
[rytynet] RTY_NET_ENABLE=1
[rytynet] GET 127.0.0.1:8080:80 -> status=200 body=13
```

`RytyNetGetStatus` fills a caller-provided status struct (`version`,
`enabled`, and the reason string for the current decision);
`RytyNetSetEnabled(1|0|other)` overrides the decision for the running process
(anything other than 0/1 restores the environment/marker decision). Both are
diagnostics for port authors, not a stable ABI.

## Known limitations

* Plain TCP HTTP/1.1 only: no TLS, no HTTP/2 (`libSceHttp2` is unchanged), no
  proxy support, no keep-alive (one connection per request).
* The request exchange blocks the calling game thread for up to 15 seconds by
  default (connect and exchange share the budget).
* The `libSceNpManager` presence is local only; there is no PSN behind it, so
  friend lists, trophies sync and store flows keep their existing behaviour.
* UDP peer-to-peer transport (voice, `libSceRudp`, session signalling) is not
  part of RytyNet yet.
* `libRytyNet.prx` must sit next to the other runtime libraries in `libs/`
  when a port is relinked with `--rytynet`, because `libSceHttp` and
  `libSceNpManager` link against it.

These limits are deliberate: RytyNet ships the smallest useful bridge first.
See [the technical debt list](../dev/TechnicalDebt.md) for the current state.
