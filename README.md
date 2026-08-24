![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-stomp)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-stomp/total)

# 4d-plugin-stomp

The STOMP plugin lets a 4D method talk directly to a [STOMP](https://stomp.github.io/) message broker (ActiveMQ, RabbitMQ's STOMP adapter, etc.) over a plain TCP socket — connecting, sending frames, and reading frames back — without going through any external process. Under the hood it drives a vendored STOMP client library on top of Apache Portable Runtime (APR) sockets; from 4D you only ever see connection IDs, text, and (depending on which command family you use) either an object or a pair of parallel arrays for frame headers.

| Command | Returns | Purpose |
|---|---|---|
| [STOMP Connect](#stomp-connect) | Longint | Open a connection to a broker, get back a connection ID |
| [STOMP Disconnect](#stomp-disconnect) | Longint | Close a connection opened with `STOMP Connect` |
| [STOMP Write](#stomp-write) | Longint | Send a STOMP frame; headers as an **object** |
| [STOMP Read](#stomp-read) | Longint | Read the next STOMP frame; headers returned as an **object** |
| [_o_STOMP Write](#_o_stomp-write) | Longint | Legacy: send a STOMP frame; headers as parallel **text arrays** |
| [_o_STOMP Read](#_o_stomp-read) | Longint | Legacy: read the next STOMP frame; headers returned as parallel **text arrays** |

**Platforms:** macOS and Windows. The plugin's source has no platform-specific branches at all — behavior is identical on both.

---

## Requirements & platform notes

- All six commands are declared thread-safe in the plugin's manifest, so 4D may call them concurrently from more than one process. See [Error handling](#error-handling--troubleshooting) for the one place this matters in practice (disconnecting a connection another process is still using).
- A connection ID from `STOMP Connect` is an opaque handle internal to the plugin (a small integer starting at 1) — it is not a socket descriptor, and IDs are reused after a connection is closed. Don't do arithmetic on it or persist it across a 4D restart.
- The plugin does not interpret or validate the `command` text you pass to `STOMP Write`/`_o_STOMP Write` at all — it's forwarded to the broker exactly as given (`"CONNECT"`, `"SEND"`, `"SUBSCRIBE"`, `"DISCONNECT"`, etc. are STOMP protocol verbs, not 4D constants).
- There are two parallel families for sending/receiving frames that differ only in how headers are packaged:
  - **Object-based** (`STOMP Read` / `STOMP Write`) — headers are properties on a 4D object. This is the more convenient form for most code and is used in the plugin's own newer sample method.
  - **Array-based** (`_o_STOMP Read` / `_o_STOMP Write`) — headers are two parallel `Text` arrays (names, values). This is the older form, kept for compatibility; the plugin's older sample method uses it.

  Both families send/receive identical STOMP frames on the wire — pick whichever header shape is more convenient for your code, and see each command's own section for the handling differences that come with that choice.
- Every `timeout` parameter across all four read/write commands is in **milliseconds**, and defaults to 3000 ms whenever you omit it or pass 0.
- `STOMP Connect` must always be paired with a later `STOMP Disconnect` for every successful connection, including on your own method's error paths — there is no automatic cleanup of an open connection until the plugin itself unloads.

---

## STOMP Connect

### Syntax

```
STOMP Connect ( host : Text ; port : Longint ) -> Longint
```

| Parameter | Type | Description |
|---|---|---|
| `host` | Text | Hostname or IP address of the STOMP broker |
| `port` | Longint | TCP port the broker is listening on (e.g. `61613`) |
| Result | Longint | Connection ID (**> 0**) to pass to every other STOMP command, or a negative value if the connection could not be opened |

### Description

`STOMP Connect` only opens the underlying TCP socket — it does not send a STOMP `CONNECT` frame. Sending the actual STOMP-level `CONNECT` frame (with login/passcode headers, if your broker requires them) is a separate `STOMP Write` call, exactly as shown in the example below.

Check `Result > 0` to confirm success, not just `Result >= 0` — a failed attempt returns a negative value (the negated underlying connection error), and `0` is never a valid connection ID.

### Example

From the plugin's own sample method (`TEST_v17.4dm`):

```4d
$stomp:=STOMP Connect ("127.0.0.1";61613)

If ($stomp>0)
	// connected — continue with STOMP Write ("CONNECT"; ...)
End if 
```

---

## STOMP Disconnect

### Syntax

```
STOMP Disconnect ( contextID : Longint ) -> Longint
```

| Parameter | Type | Description |
|---|---|---|
| `contextID` | Longint | Connection ID returned by `STOMP Connect` |
| Result | Longint | `0` on success. See the note below — this command's failure encoding differs from every other command in the plugin. |

### Description

Closes the socket and releases the connection ID. After this call, `contextID` is no longer valid — passing it to any other STOMP command returns `-1` ("not found"), the same value you'd get for any unrecognized connection ID.

**Unlike every other command in this plugin, a failure from `STOMP Disconnect` is returned as-is, not negated.** Every other command here follows "0 = success, negative = error"; `STOMP Disconnect` returns the underlying disconnect status directly when the connection ID is found, so a failure here can come back as a small positive number rather than a negative one. Treat any non-zero result from `STOMP Disconnect` as failure, regardless of its sign — don't rely on "negative = error" for this one command specifically.

Avoid calling `STOMP Disconnect` for a connection ID that another concurrent process might still be using with `STOMP Read`/`STOMP Write` — see [Error handling](#error-handling--troubleshooting).

### Example

```4d
$err:=STOMP Disconnect ($stomp)
```

---

## STOMP Write

### Syntax

```
STOMP Write ( contextID : Longint ; command : Text ; body : Text ; headers : Object ; timeout : Longint ) -> Longint
```

| Parameter | Type | Description |
|---|---|---|
| `contextID` | Longint | Connection ID from `STOMP Connect` |
| `command` | Text | STOMP frame command (e.g. `"CONNECT"`, `"SEND"`, `"SUBSCRIBE"`, `"DISCONNECT"`), sent to the broker exactly as given |
| `body` | Text | Frame body; pass `""` for frames with no body |
| `headers` | Object | Frame headers as object properties — property name = header name, property value = header text. Optional; omit or pass `Null` for no headers. |
| `timeout` | Longint | Socket timeout in milliseconds; `0` or omitted = 3000 ms |
| Result | Longint | `0` on success, negative on failure |

### Description

Only **string-valued** properties on `headers` are sent as STOMP headers — a number, boolean, or date property is silently skipped (no error). Since the plugin only *reads* `headers`, it's safe to build one object and reuse it across several `STOMP Write` calls in a row, as shown below.

### Example

From the plugin's own sample method (`TEST_v17.4dm`):

```4d
$headers:=New object:C1471

$headers.login:="passcode"
$headers.passcode:="admin"

$err:=STOMP Write ($stomp;"CONNECT";"";$headers)

  //NOTE: the array pairs are unchanged for STOMP write
If ($err=0)
	
	$headers:=New object:C1471
	
	$err:=STOMP Read ($stomp;$command;$body;$headers)
	
	If ($err=0) & ($command="CONNECTED")
		
		$headers:=New object:C1471
		
		$headers.destination:="/queue/FOO.BAR"
		
		$err:=STOMP Write ($stomp;"SUBSCRIBE";"";$headers)
		
		$body:="This is the message"
		
		$err:=STOMP Write ($stomp;"SEND";$body;$headers)
```

A frame with no headers (a bare `DISCONNECT`) can simply omit `headers`:

```4d
$err:=STOMP Write ($stomp;"DISCONNECT")
```

---

## STOMP Read

### Syntax

```
STOMP Read ( contextID : Longint ; command : Text ; body : Text ; headers : Object ; timeout : Longint ) -> Longint
```

| Parameter | Type | Description |
|---|---|---|
| `contextID` | Longint | Connection ID from `STOMP Connect` |
| `command` | Text | Output: the frame's STOMP command (e.g. `"CONNECTED"`, `"MESSAGE"`, `"ERROR"`) |
| `body` | Text | Output: the frame body (`""` if none) |
| `headers` | Object | Output — see the caveat below before reusing an object across calls |
| `timeout` | Longint | Milliseconds to wait for a frame before giving up; `0` or omitted = 3000 ms |
| Result | Longint | `0` on success, negative on failure or timeout |

### Description

`command` and `body` are always returned with real values — on failure they come back as `""`, never left holding whatever `$command`/`$body` contained before the call.

**`headers` only ever has properties added to it — it is never cleared first, and it is left completely untouched if the incoming frame has no headers at all.** If you reuse the same object across multiple `STOMP Read` calls without resetting it, headers from an earlier frame will still be present alongside (or instead of, if the new frame is headerless) whatever the latest frame actually sent. Always pass a freshly created object (`New object`) before each call, exactly as the sample below does — don't assume `headers` comes back empty just because the frame had none.

### Example

From the plugin's own sample method (`TEST_v17.4dm`):

```4d
$headers:=New object:C1471

$err:=STOMP Read ($stomp;$command;$body;$headers)

If ($err=0) & ($command="CONNECTED")
```

---

## _o_STOMP Write

### Syntax

```
_o_STOMP Write ( contextID : Longint ; command : Text ; body : Text ; headerNames : Text array ; headerValues : Text array ; timeout : Longint ) -> Longint
```

| Parameter | Type | Description |
|---|---|---|
| `contextID` | Longint | Connection ID from `STOMP Connect` |
| `command` | Text | STOMP frame command, forwarded verbatim |
| `body` | Text | Frame body; pass `""` for frames with no body |
| `headerNames` | Text array | Header names; `headerNames{i}` pairs with `headerValues{i}` |
| `headerValues` | Text array | Header values, same length as `headerNames` |
| `timeout` | Longint | Socket timeout in milliseconds; `0` or omitted = 3000 ms |
| Result | Longint | `0` on success, negative on failure |

### Description

This is the array-based counterpart to [`STOMP Write`](#stomp-write) — same frame semantics, headers packaged as two parallel arrays instead of an object. **`headerNames` and `headerValues` must be the same length, or no headers are sent at all** — a size mismatch is not an error, it's a silent no-headers write. An empty name at a given index is likewise skipped.

The plugin only reads these arrays and never writes back into them, so — as the sample's own comment notes — you can reuse the same pair of arrays across multiple calls without resetting them.

### Example

From the plugin's own sample method (`TEST.4dm`):

```4d
ARRAY TEXT:C222($headerNames;2)
ARRAY TEXT:C222($headerValues;2)

$headerNames{1}:="login"
$headerNames{2}:="passcode"
$headerValues{1}:="admin"
$headerValues{2}:="admin"

$err:=_o_STOMP Write ($stomp;"CONNECT";"";$headerNames;$headerValues)

  //NOTE: the array pairs are unchanged for STOMP write
If ($err=0)
	
	$err:=_o_STOMP Read ($stomp;$command;$body;$headerNames;$headerValues)
	
	If ($err=0) & ($command="CONNECTED")
		
		ARRAY TEXT:C222($headerNames;1)
		ARRAY TEXT:C222($headerValues;1)
		
		$headerNames{1}:="destination"
		$headerValues{1}:="/queue/FOO.BAR"
		
		$err:=_o_STOMP Write ($stomp;"SUBSCRIBE";"";$headerNames;$headerValues)
```

A frame with no headers can pass empty (or zero-size) arrays, or simply omit trailing parameters, as the sample does for `DISCONNECT`:

```4d
$err:=_o_STOMP Write ($stomp;"DISCONNECT")
```

---

## _o_STOMP Read

### Syntax

```
_o_STOMP Read ( contextID : Longint ; command : Text ; body : Text ; headerNames : Text array ; headerValues : Text array ; timeout : Longint ) -> Longint
```

| Parameter | Type | Description |
|---|---|---|
| `contextID` | Longint | Connection ID from `STOMP Connect` |
| `command` | Text | Output: the frame's STOMP command |
| `body` | Text | Output: the frame body (`""` if none) |
| `headerNames` | Text array | Output: header names, one per received header |
| `headerValues` | Text array | Output: header values, same index as `headerNames` |
| `timeout` | Longint | Milliseconds to wait for a frame; `0` or omitted = 3000 ms |
| Result | Longint | `0` on success, negative on failure or timeout |

### Description

This is the array-based counterpart to [`STOMP Read`](#stomp-read). Unlike that command's object form, `headerNames`/`headerValues` are rebuilt from scratch on every call, so there's no stale-data carryover to worry about between reads.

One thing worth verifying against your actual broker before relying on it: the arrays are pre-sized before headers are appended into them, and depending on the exact array-append behavior this can plausibly leave a single extra blank entry at the front of `headerNames`/`headerValues` when a frame does carry headers. This isn't confirmed against a live broker — if you index directly into these arrays (rather than looping over their full current size and skipping blanks), check the first element the first time you use this command, or prefer [`STOMP Read`](#stomp-read) if you'd rather sidestep the question entirely.

### Example

From the plugin's own sample method (`TEST.4dm`):

```4d
$err:=_o_STOMP Read ($stomp;$command;$body;$headerNames;$headerValues)

If ($err=0) & ($command="CONNECTED")
```

---

## Error handling & troubleshooting

- **Result codes are 0-for-success / negative-for-failure everywhere except `STOMP Disconnect`.** That one command returns its underlying status as-is rather than negated — check for non-zero, not specifically negative, when testing its result.
- **`STOMP Read`'s `headers` object is additive, never cleared.** Pass a freshly created object (`New object`) before every call, or you'll accumulate headers from earlier frames — and a headerless frame leaves `headers` completely untouched rather than resetting it to empty.
- **`_o_STOMP Write`'s two arrays must match in length, or the whole write silently sends no headers.** There's no error returned for a size mismatch — check `Size of array` on both before calling if you build them dynamically.
- **`STOMP Write`'s object form only forwards string properties.** A numeric, boolean, or date property on `headers` is silently dropped rather than converted or flagged.
- **Timeouts are milliseconds, and `0` means "use the 3000 ms default," not "no timeout."** Avoid passing a negative timeout — the plugin does not currently treat a negative value the same as `0`, and it can end up waiting far longer than intended instead of falling back to the default.
- **Don't disconnect a connection another process might still be using.** Because every command here is thread-safe per the manifest, 4D can call them concurrently — calling `STOMP Disconnect` on a `contextID` while a `STOMP Read`/`STOMP Write` on that same ID is still in flight elsewhere is not safe in the current build and can crash 4D. Structure your code so a given connection ID is only ever disconnected once you know nothing else is using it.
- **A connection you forget to disconnect stays open** until the plugin itself unloads — always route every `STOMP Connect` success through a matching `STOMP Disconnect`, including your own error/early-return paths.

---

## Quick reference

```4d
$stomp:=STOMP Connect ("127.0.0.1";61613)

If ($stomp>0)
	
	$headers:=New object:C1471
	$headers.login:="admin"
	$headers.passcode:="admin"
	$err:=STOMP Write ($stomp;"CONNECT";"";$headers)
	
	If ($err=0)
		
		$headers:=New object:C1471
		$err:=STOMP Read ($stomp;$command;$body;$headers;5000)
		
		If ($err=0) & ($command="CONNECTED")
			
			$headers:=New object:C1471
			$headers.destination:="/queue/FOO.BAR"
			$err:=STOMP Write ($stomp;"SUBSCRIBE";"";$headers)
			
			$err:=STOMP Write ($stomp;"SEND";"Hello";$headers)
			
			$headers:=New object:C1471
			$err:=STOMP Read ($stomp;$command;$body;$headers;5000)
			
			$err:=STOMP Write ($stomp;"DISCONNECT")
			
		End if 
		
	End if 
	
	$err:=STOMP Disconnect ($stomp)
	
End if 
```
