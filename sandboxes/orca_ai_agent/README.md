# Orca AI Agent

Gemini-backed chat panel for OrcaSlicer. The plugin can explain print settings and
propose a constrained set of process or filament preset changes for review before
anything is applied.

## Loading the plugin

Load `plugin.py` as a side-loaded Python plugin. The entry file embeds the panel,
so it remains self-contained when an installer copies only that file. A checkout
uses `ui/panel.html` directly when it is present.

Enter a Google AI Studio API key in the panel. The key is kept only in the
panel's memory unless **Save key in browser storage** is explicitly selected;
browser storage is not encrypted and may be shared with other panels using the
same WebView origin. Prompts, chat history, and the selected preset context are
sent to Google Gemini.

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
