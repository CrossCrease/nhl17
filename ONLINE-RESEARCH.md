# NHL 17 online protocol research

The static client evidence identifies the bundled SDK as
`BlazeSDK/15.1.1.3.0-nhl.01` in both the base and patched executables. The
installed update reports NHL patch `1.08` and online version `NHL17_1.08`.

Live capture is opt-in. It records guest socket payload chunks before the host
socket call, plus DNS names, connection attempts, socket IDs, timestamps, and
directions. Start a session with:

```powershell
.\tools\start_nhl17_online_capture.ps1 -Label online-boot-01
```

After completing the requested in-game steps, close the game normally and run:

```powershell
.\tools\finish_nhl17_online_capture.ps1 -Label online-boot-01
```

The capture is stored under `artifacts/online-captures/<label>`. This directory
is ignored by Git. Raw `.bin` payloads can contain credentials, authorization
codes, account identifiers, or other private data. Never publish or commit raw
captures. `summary.json` exposes only endpoints, hashes, frame metadata, and a
small allowlist of protocol/version fields.

The NHL 24 Blaze 16.1.0 implementation is useful for FIRE2 framing, TDF tooling,
capture hygiene, and server organization. Its component schemas and response
payloads are not presumed compatible with NHL 17.
