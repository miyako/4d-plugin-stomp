# Native string ownership audit

Baseline: `64d99735d7538687118ea02ab1be76999abfaa3f`.
Guidance: `miyako/skills` merged PRs 69 and 71, at
`e6b492fcb81e5e1a7f7db723594b420e63bf6eb9`.
The bundled SDK implementation labels itself `rev: v14`; its header labels
itself `v13`. These labels are not verified upstream release identities.
The regression checks every tracked SDK/header/wrapper byte against the baseline.

## Inventory and reachability

Both `manifest.json` and the tracked bundle resource manifest declare the same
six commands. `PluginMain` dispatches selectors 1-6 to those commands in order.
The `&Y` declarations alone do not prove text arrays; the legacy command bodies
instantiate `ARRAY_TEXT`, whose implementation requires `eVK_ArrayUnicode`.

| Surface | Ownership/reachability conclusion |
|---|---|
| `STOMP Connect`, `STOMP Disconnect`, startup/exit | Hostname is copied to a local UTF-8 owner through the synchronous connect call. Contexts hold connections/pools, not SDK strings. No callback-returned string variables. |
| `_o_STOMP Read` | Header callbacks append independent UTF-16 copies to two wrappers; `toParamAtIndex` creates a separate SDK descriptor for every element/output and transfers it without premature disposal. |
| `_o_STOMP Write` | Borrowed parameter elements are copied before use. Confirmed downstream lifetime defect: repeated UTF-8 conversion overwrote buffers retained by the APR hash before writing. Fixed with reserved per-call independent snapshots. |
| `STOMP Read` | `get_frame_header_obj` calls the narrow `ob_set_s`, exposing the scalar empty-string overwrite leak. The input property object remains caller-owned. |
| `STOMP Write` | `ob_stringify` owns its duplicated object argument and clears it after the synchronous command. It converts the returned string before clearing the result, including unexpected result kinds. Confirmed downstream lifetime defect: loop-local header name/value buffers died before the write. Fixed with reserved per-call independent snapshots. |
| `C_TEXT`, `ARRAY_TEXT` | Both are compiled by Xcode and Visual Studio. Scalar content setters and array descriptor transfers are distinct contracts. Input wrappers copy before owner mutation; output arrays have independent owners. No SDK cleanup change was needed. |
| JSON string setter overloads | All four are compiled; only narrow-key `ob_set_s` is command-reachable here. All overwrote the initially owned empty string. Start Undefined before the shallow transfer instead. |
| JSON property getters | `ob_get_a/b/n/c` are compiled but dormant in current commands. Clear successful owning results after copying, including mismatches. Matching collection results retain their existing owning handoff. Failed reads are logged and their unspecified outputs are neither inspected nor cleared. |
| Other JSON helpers | Picture/object/collection/numeric/boolean/null setters and `ob_is_defined` are compiled, currently dormant. Existing successful owning handoffs/cleanup are preserved; no speculative picture ownership changes. |
| `C_LONGINT.cpp/.h` | Tracked but not in either project compile list; scalar-only, no string ownership. |

No tracked first-party `CPicture`/`C_POINTER` class, application-path output
consumer, picture-data output consumer, method callback, or asynchronous string
callback exists in this repository. SDK implementations of otherwise unused APIs
are contract evidence, not newly reachable plugin helpers. Vendored APR, jsoncpp,
and libstomp implementations are excluded from the first-party audit; the latter
was read only to establish when it consumes first-party header buffers.

## Contracts established from the bundled SDK

- `PA_CreateVariable(eVK_Unistring)` calls `PA_CreateUnistring` for an empty
  buffer. `PA_SetStringVariable` only assigns the type/filler/descriptor, so
  overwriting an allocated empty leaks it. Null and allocated empties are tested.
- `PA_GetObjectProperty` explicitly documents caller cleanup of returned
  variables. Its local output is not initialized before the engine call.
  Cleanup/type inspection is therefore gated on the immediate SDK error result;
  no owning output is assumed on failure.
- `PA_SetStringInArray` checks Unicode type, non-null handle, nonnegative
  index through `fNbElements`, and non-null descriptor pointer. On acceptance
  it disposes the old element and shallow-transfers the incoming descriptor;
  rejection leaves the allocation with the caller. Index zero is accepted.
- `PA_GetStringInArray` explicitly forbids modification of the returned alias.
  Wrappers copy it before the source is replaced or cleared.
- `PA_SetUnistring` forwards existing storage and UTF-16 content to an engine
  content setter. It is not the array descriptor-transfer implementation.
  CoreFoundation conversion lengths used here are UTF-16 units for character
  access and bytes for UTF-8 output; exact outputs and terminators are checked.
- APR hashes retain pointers rather than owning C++ buffers. The vendored
  `stomp_write` reads the header pointers synchronously, after construction of
  the hash. Reserved vectors keep each independent snapshot stable until that
  call returns, including its error path, without connection-lifetime storage.

## Reproducible regressions

Run on Intel macOS, or Apple Silicon with Rosetta:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/test_ownership.py -v
```

The runner compiles the actual dispatcher, JSON helpers, C_TEXT, ARRAY_TEXT,
and full unchanged SDK implementation. The SDK is compiled as C++, as in the
Windows project, not rewritten or replaced with setter substitutes. It links
the unchanged bundled x86_64 APR/jsoncpp archives. Only the 4D engine callback,
STOMP network operations, and socket timeout operation are synthetic; no broker,
socket connection, credentials, or external service is used. Unexpected engine
selectors and network connection/read attempts abort.

The same final harness is compiled against immutable baseline sources and the
working sources. Expected baseline failures are asserted, not ignored:

| Baseline scenario | Required failure |
|---|---|
| Four setters, allocated empties | 48 unreleased string allocations |
| Property getters, allocated empties | 48 unreleased strings; collection reference count 3 instead of 2 |
| Property getters, null empties | 36 unreleased strings; collection reference count 3 instead of 2 |
| Object write | ASan heap-use-after-free during delayed header consumption |
| Legacy array write | ASan heap-use-after-free during delayed header consumption |

Baseline setters with null empties pass, avoiding a manufactured allocation
contract. Fixed sources pass all seven scenarios with both empty
representations. Unchanged array/scalar paths also pass against the baseline.
Assertions cover exact empty/ASCII/BMP/supplementary text and UTF-16 lengths;
replacement, shrink, clear, borrowed-source copies, independent output arrays;
rejected type/index/null-handle/null-string setter destinations; successful
collection handoff and owning mismatch cleanup; invalid non-owning failure
outputs; duplicated synchronous stringify arguments and independently allocated
or unexpected owning results; long multiple headers, duplicate names, ignored
nonstring properties, headerless/mismatched-array frames, repeated writes, and
synchronous write failures.

ASan and UBSan are enabled, including alignment checks for the packed SDK ABI.
LeakSanitizer is disabled on macOS; explicit SDK string, handle, object, and
collection allocation/reference accounting supplies the leak assertions.
Bundled third-party archives are not sanitizer-instrumented.

The new read-only PR/default-branch CI job runs this suite without any release
steps. Existing workflow-contract tests still require Build and Bump to remain
independent manual operations; the separate ownership workflow is explicitly
checked as test-only. The migration-era immutable-tree test exempts only the
intentionally audited `support/4DPlugin-JSON.cpp`, not the rest of `support/`,
SDK, dependencies, manifests, project files, or prebuilt bundles.

## Limits and deferred contracts

This is synthetic engine-boundary evidence, locally on macOS with x86_64
execution under Rosetta, not a 4D host or Windows crash reproduction. Windows
conversions and a complete Xcode/plugin packaging build were not executed.
The standalone SDK C++ build does not certify the legacy SDK's C-mode header
compatibility. No signing, notarization, release/Deployment build, packaging,
upload, tag, release dispatch, or binary publication is part of validation.

Array allocation/growth failures remain deferred: the SDK may update the
element count even if growing the handle fails, and these normal-array tests
do not certify wrapper allocation-failure behavior. Direct setter rejection
tests do not claim otherwise. Failed object-property reads intentionally carry
no owning-output assumption. Object/collection engine copy/reference behavior
is modeled consistently with the SDK's owning getter and existing setter
usage, not claimed as independently certified against a real host.
Unused pointer metadata and undocumented application-path/picture-data output
ownership are not assigned guessed disposal rules.
