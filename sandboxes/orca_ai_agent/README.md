# Orca AI Agent

Gemini-backed chat panel for OrcaSlicer. The plugin can explain print settings and
propose a constrained set of process or filament preset changes for review before
anything is applied.

## Loading the plugin

Load `plugin.py` as a side-loaded Python plugin. The entry file embeds the panel,
so it remains self-contained when an installer copies only that file. A checkout
uses `ui/panel.html` directly when it is present.

The plugin has **no PEP 723 dependencies** and uses only the standard library (`urllib`) against
Gemini's OpenAI-compatible endpoint (`https://generativelanguage.googleapis.com/v1beta/openai/chat/completions`).
Hosts without the bundled `uv` fail to load plugins that declare dependencies, so keep it that way.
TLS uses the OS trust store (`truststore`), `certifi`, or a system CA bundle when available, because
OrcaSlicer's embedded OpenSSL may not find CA certificates by itself.

Enter a Google AI Studio API key in the panel. The key is kept only in the
panel's memory unless **Save key in browser storage** is explicitly selected;
browser storage is not encrypted and may be shared with other panels using the
same WebView origin. Prompts, chat history, and the selected preset context are
sent to Google Gemini.

## Safety guardrails

Proposals are validated before they reach the preview and again on Apply:

- **Hard limits** (the proposal is dropped): whitelisted keys only, type/shape checks, plausible FDM
  bounds (for example 0-30 wall loops, 150-450 C nozzle, 0-150 C High Temp Plate), layer height not
  above the nozzle diameter, process speeds not above the printer's `machine_max_speed_x/y`.
- **Cautions** (shown in the preview, never silently dropped): changes of more than 50%, nozzle
  temperature outside the filament's recommended range, first-layer/normal temperature mismatch,
  layer height outside the printer's adaptive-layer limits, support settings while support is off,
  first-layer speed above the outer-wall speed, unusual flow ratios.

## Threading model (no asyncio)

`execute()` is synchronous and only opens the panel. The plugin never creates an event loop: on
Windows `asyncio.new_event_loop()` calls `socket.bind` through its self-pipe, which the plugin audit
hook rejects (`Plugin attempted audited operation "socket.bind" without permission`).

- `on_message()` runs on the UI thread: it reads the live presets, answers cheap actions, applies
  presets (`begin_update` / `commit`), and starts one worker thread per chat request.
- The chat worker waits for the HTTP thread through a `queue.Queue` with a deadline and a
  `threading.Event` for cancellation (`WORKER_POLL_SECONDS`). A blocking socket call cannot be
  interrupted, so a cancelled or timed-out HTTP thread is abandoned: its result is dropped and the
  socket ends by its own 60 s timeout.
- Cancel button / panel timeout / closing the panel / plugin unload all cancel the request. A late
  answer is never posted and never enters the history (a cancel after the answer rolls it back).
- A 400 from the compat endpoint is retried once without the optional fields (`max_tokens`,
  `reasoning_effort`, JSON schema). Truncated or filtered answers produce explicit errors.

## Automatic retries

Transient failures (HTTP 500/502/503/504, connection resets, socket timeouts) are retried up to
`MAX_TRANSIENT_RETRIES` (3) times with exponential backoff (about 1 s, 2 s, 4 s, plus up to 25%
jitter). Authentication, quota (429) and request errors are never retried. Every wait is
interruptible: the Cancel button, a panel timeout, closing the panel and the 75 s total deadline all
stop the sequence at once (a backoff that would not fit before the deadline is skipped). While
retrying, the panel shows "Gemini is busy. Retrying in N s (attempt k/3)". If a 503 persists, the
error says Gemini is seeing a temporary surge in requests and suggests trying again or switching
model.

## Model list

**Fetch Models** (next to the model field) sends `fetch_models` to the backend, which calls the
provider's model-list endpoint (`GET .../v1beta/openai/models`) with the typed API key in the
`Authorization` header. Only chat-capable `gemini*` / `gemma*` models are kept (embeddings, image,
video, speech, live and similar are filtered out), de-duplicated and sorted newest first. The list
fills a datalist and a dropdown and is cached in the panel's local storage (names only). The model
field always stays editable, so a custom model name works when fetching fails. The request is
cancellable (the button turns into Cancel), has a 25 s deadline, retries transient errors, and the
key is redacted from every error shown. Fetching and chatting are mutually exclusive.

Permissions requested: the chat URL and the model-list URL, nothing else.

Permissions: only the two fixed Gemini URLs (chat and model list) are requested as `network`.
No file-system, socket or process permission is needed.

## Versioning

`PLUGIN_VERSION` in `plugin.py` is the single source of truth. The PEP 723 header
(`version = "..."`) must carry the same value; `test_plugin.py` fails if they differ or if the
version is a legacy `1.0`. The version is stamped into the panel (`v0.2.0` next to the title) and
sent in every `state` message. To release: change both values, run `python embed_panel.py`
(the panel only holds a placeholder, so this is only needed when the panel itself changed), run the
tests, then reinstall the plugin so OrcaSlicer re-reads its metadata. A version cached by the host
(for example `installed_version` in `.install_state.json`) only updates on reinstall.

## Developing

`plugin.py` embeds a compressed copy of `ui/panel.html`. After editing the panel, regenerate it:

```bash
python embed_panel.py          # rewrite EMBEDDED_PANEL_HTML in plugin.py
python embed_panel.py --check  # CI: fail when the embedded copy is stale
python -m unittest test_plugin -v   # offline tests (fake host, fake google-genai)
```

## Rebuild note

This branch changes the native preset transaction API as well as the plugin.
Rebuild OrcaSlicer before testing the plugin; reloading only `plugin.py` against
an older binary will not provide the atomic `commit(expected_current=...)`
conflict check used by Apply.

Build the Release configuration with tests enabled, then run the test suite:

```bash
cmake --build build --config Release --target slic3rutils_tests
ctest --test-dir build/tests -C Release --output-on-failure
```

The Python and JavaScript behavior was exercised with mocked Gemini and host
bridges. A live Gemini request still requires a valid API key and was not part of
the local verification.
