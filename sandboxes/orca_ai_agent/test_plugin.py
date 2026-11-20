"""Offline tests for the Orca AI Agent plugin (fake `orca` host and fake `google.genai`).

Run:  python -m unittest test_plugin -v
"""

import asyncio
import importlib.util
import re
import socket
import sys
import threading
import time
import tomllib
import types
import unittest
from pathlib import Path
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
PLUGIN_PATH = HERE / "plugin.py"
PANEL_PATH = HERE / "ui" / "panel.html"

# The host's plugin audit hook rejects socket.bind (what an asyncio event loop does on Windows).
# This permanent hook only records events while a test has switched it on.
AUDIT_STATE = {"enabled": False, "events": []}


def _audit_hook(event, args):
    if AUDIT_STATE["enabled"] and event in {"socket.bind"}:
        AUDIT_STATE["events"].append((event, args))


sys.addaudithook(_audit_hook)


def expected_html(version):
    return PANEL_PATH.read_text(encoding="utf-8").replace("__PLUGIN_VERSION__", version)


def wait_until(predicate, timeout=5.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return True
        time.sleep(0.01)
    return False


# --------------------------------------------------------------------------- fake host
class FakePanel:
    def __init__(self, **kwargs):
        self.kwargs = kwargs
        self.posted = []
        self.open = True

    def is_open(self):
        return self.open

    def show(self):
        pass

    def close(self):
        self.open = False

    def post(self, message):
        self.posted.append(message)


class _HostBaseMeta(type):
    """Mimics the host: instantiating a subclass that overrides __init__ without calling the base
    __init__ raises the same TypeError that failed plugin registration."""

    def __call__(cls, *args, **kwargs):
        instance = super().__call__(*args, **kwargs)
        base_init = next(b for b in cls.__mro__ if b.__dict__.get("_is_host_base"))
        if "__init__" in cls.__dict__ or cls.__init__ is not base_init.__init__:
            if not instance.__dict__.get("_host_base_init_done"):
                raise TypeError(
                    f"{base_init.__module__}.{base_init.__name__}.__init__() must be called "
                    "when overriding __init__"
                )
        return instance


class FakePreset:
    def __init__(self, name, values):
        self.name, self._values = name, values

    def config_value(self, key):
        return self._values[key]  # KeyError for unknown keys, like a missing option


def make_fake_bundle():
    process = FakePreset(CONTEXT["process_preset"], CONTEXT["process"])
    printer = FakePreset(CONTEXT["printer_preset"], CONTEXT["printer"])
    filaments = [FakePreset(f["name"], f["values"]) for f in CONTEXT["filaments"]]
    return SimpleNamespace(
        current_process_preset=lambda: process,
        current_printer_preset=lambda: printer,
        current_filament_presets=lambda: filaments,
        full_config_value=lambda key: CONTEXT["bed_type"],
    )


def make_fake_orca():
    orca = types.ModuleType("orca")
    orca.base_init_events = []

    def make_base(name, module):
        def __init__(self):
            # Record whether subclass state already existed when the base was initialized.
            orca.base_init_events.append((name, "panel" in self.__dict__))
            self._host_base_init_done = True

        return _HostBaseMeta(name, (), {"__init__": __init__, "_is_host_base": True, "__module__": module})

    orca.script = SimpleNamespace(ScriptPluginCapabilityBase=make_base("ScriptPluginCapabilityBase", "orca.script"))
    orca.base = make_base("base", "orca")
    orca.plugin = lambda cls: cls
    orca.permission_calls = []
    orca.request_permissions = lambda **kw: orca.permission_calls.append(kw)
    orca.registered = []
    orca.register_capability = lambda cls: orca.registered.append(cls)

    class ExecutionResult:
        def __init__(self, ok, message):
            self.ok, self.message = ok, message

        @staticmethod
        def success(message):
            return ExecutionResult(True, message)

        @staticmethod
        def failure(code, message):
            return ExecutionResult(False, message)

    orca.ExecutionResult = ExecutionResult
    orca.PluginResult = SimpleNamespace(RecoverableError="RecoverableError")
    orca.panels = []

    def create_dock_panel(**kwargs):
        panel = FakePanel(**kwargs)
        orca.panels.append(panel)
        return panel

    orca.commits = []

    class Transaction:
        def __init__(self, category, name, slot=None):
            self.category, self.name, self.slot, self.staged = category, name, slot, {}

        def set(self, key, value):
            self.staged[key] = value

        def commit(self, expected_current=None):
            orca.commits.append({"category": self.category, "name": self.name, "staged": dict(self.staged),
                                 "expected": dict(expected_current or {}), "thread": threading.current_thread()})
            return SimpleNamespace(category=self.category, target_name=self.name, keys=list(self.staged),
                                   dirty=True, selection_changed=False, slot_assignment_changed=False,
                                   slot_number=self.slot or 0, shared_slots=[self.slot] if self.slot else [])

    orca.host = SimpleNamespace(
        ui=SimpleNamespace(create_dock_panel=create_dock_panel),
        preset=SimpleNamespace(
            process=SimpleNamespace(begin_update=lambda preset_name: Transaction("process", preset_name)),
            filament=SimpleNamespace(begin_update=lambda slot_number, preset_name: Transaction("filament", preset_name, slot_number)),
        ),
        preset_bundle=lambda: make_fake_bundle(),
    )
    return orca


def load_plugin(name="orca_ai_agent_plugin_under_test"):
    fake = make_fake_orca()
    sys.modules["orca"] = fake
    spec = importlib.util.spec_from_file_location(name, PLUGIN_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, fake


# --------------------------------------------------------------------------- fake transport
def completion(text="", finish="stop"):
    return {"choices": [{"finish_reason": finish, "message": {"role": "assistant", "content": text}}]}


CONTEXT = {
    "process_preset": "0.20mm Standard",
    "printer_preset": "Test Printer",
    "bed_type": "High Temp Plate",
    "process": {
        "layer_height": "0.2", "wall_loops": "2", "top_shell_layers": "4", "bottom_shell_layers": "3",
        "sparse_infill_density": "15%", "sparse_infill_pattern": "gyroid",
        "initial_layer_speed": "50", "outer_wall_speed": "100", "inner_wall_speed": "150",
        "sparse_infill_speed": "200", "enable_support": "0", "support_threshold_angle": "30",
        "support_style": "default",
    },
    "printer": {
        "nozzle_diameter": "0.4", "min_layer_height": "0.07", "max_layer_height": "0.32",
        "machine_max_speed_x": "500,200", "machine_max_speed_y": "500,200",
    },
    "filaments": [{
        "slot": 1, "name": "Generic PLA", "shared_slots": [1],
        "values": {
            "filament_flow_ratio": "0.98", "filament_density": "1.24",
            "filament_retraction_length": "0.8", "filament_retraction_speed": "30",
            "filament_max_volumetric_speed": "15", "nozzle_temperature": "210",
            "nozzle_temperature_initial_layer": "215", "hot_plate_temp": "60",
            "hot_plate_temp_initial_layer": "60", "filament_type": "PLA",
            "nozzle_temperature_range_low": "190", "nozzle_temperature_range_high": "230",
        },
    }],
}


def norm(cap, key, value, ctx=CONTEXT, category="process"):
    values = cap._values_for_category(category, ctx)
    return cap._normalize_parameter_value(key, value, current_value=values[key], context=ctx)


class PluginTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mod, cls.fake = load_plugin()
        cls.cap_cls = cls.mod.AIAgentCapability

    # ---- B / packaging
    def test_pep723_metadata_matches_working_reference(self):
        src = PLUGIN_PATH.read_text(encoding="utf-8")
        block = re.search(r"(?m)^# /// script$(.*?)^# ///$", src, re.S).group(1)
        meta = tomllib.loads("\n".join(line[2:] if line.startswith("# ") else line[1:] for line in block.strip().splitlines()))
        # No PEP 723 dependencies: hosts without the bundled uv fail to load the whole plugin.
        self.assertNotIn("dependencies", meta)
        self.assertNotIn("google", src.split('"""', 1)[0].lower())
        self.assertFalse(re.search(r"(?m)^\s*(from|import)\s+(google|httpx)\b", src))
        plugin_meta = meta["tool"]["orcaslicer"]["plugin"]
        for field in ("name", "description", "author", "version"):
            self.assertIn(field, plugin_meta)
        self.assertEqual(plugin_meta["name"], "Orca AI Agent")
        self.assertFalse(re.search(r"(?m)^# dependencies", src))

    def test_registration_and_name(self):
        order = []
        self.fake.request_permissions = lambda **kw: order.append(("perm", kw))
        self.fake.register_capability = lambda cls: order.append(("cap", cls))
        self.mod.OrcaAIAgentPackage().register_capabilities()
        self.assertEqual([o[0] for o in order], ["perm", "cap"])
        self.assertEqual(order[0][1], {"network": [self.mod.GEMINI_CHAT_URL, self.mod.GEMINI_MODELS_URL]})
        self.assertIs(order[1][1], self.cap_cls)
        name = self.cap_cls().get_name()
        self.assertNotIn(";", name)
        self.assertNotEqual(name, "Orca AI Agent")

    def test_registration_survives_missing_or_failing_request_permissions(self):
        registered = []
        self.fake.register_capability = registered.append
        saved = self.fake.request_permissions
        try:
            del self.fake.request_permissions
            self.mod.OrcaAIAgentPackage().register_capabilities()
            def boom(**kw):
                raise RuntimeError("not allowed here")
            self.fake.request_permissions = boom
            self.mod.OrcaAIAgentPackage().register_capabilities()
        finally:
            self.fake.request_permissions = saved
        self.assertEqual(len(registered), 2)

    def test_script_base_resolved_when_only_importable_submodule(self):
        fake = make_fake_orca()
        script = fake.script
        del fake.script  # attribute missing, as on some builds
        sub = types.ModuleType("orca.script")
        sub.ScriptPluginCapabilityBase = script.ScriptPluginCapabilityBase
        sys.modules["orca"] = fake
        sys.modules["orca.script"] = sub
        try:
            spec = importlib.util.spec_from_file_location("orca_ai_agent_nosub", PLUGIN_PATH)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            self.assertTrue(issubclass(module.AIAgentCapability, sub.ScriptPluginCapabilityBase))
        finally:
            sys.modules.pop("orca.script", None)
            sys.modules["orca"] = self.fake

    def test_missing_orca_script_raises_clear_error(self):
        fake = make_fake_orca()
        del fake.script
        sys.modules["orca"] = fake
        sys.modules.pop("orca.script", None)
        try:
            spec = importlib.util.spec_from_file_location("orca_ai_agent_noscript", PLUGIN_PATH)
            module = importlib.util.module_from_spec(spec)
            with self.assertRaisesRegex(ImportError, "orca.script"):
                spec.loader.exec_module(module)
        finally:
            sys.modules["orca"] = self.fake

    def test_embedded_panel_matches_source(self):
        self.mod.PANEL_FILE = None
        try:
            self.assertEqual(self.mod._load_panel_html(), expected_html(self.mod.PLUGIN_VERSION))
        finally:
            self.mod.PANEL_FILE = PANEL_PATH

    # ---- versioning
    def test_version_is_consistent_and_not_1_0(self):
        src = PLUGIN_PATH.read_text(encoding="utf-8")
        block = re.search(r"(?m)^# /// script$(.*?)^# ///$", src, re.S).group(1)
        meta = tomllib.loads("\n".join(line[2:] if line.startswith("# ") else line[1:] for line in block.strip().splitlines()))
        header_version = meta["tool"]["orcaslicer"]["plugin"]["version"]
        self.assertEqual(header_version, self.mod.PLUGIN_VERSION)
        self.assertEqual(self.mod.__version__, self.mod.PLUGIN_VERSION)
        self.assertRegex(self.mod.PLUGIN_VERSION, r"^\d+\.\d+\.\d+$")
        for old in ("1.0", "v1.0", "1.0.0", "v1.0.0"):
            self.assertNotEqual(header_version, old)
            self.assertNotEqual(self.mod.PLUGIN_VERSION, old)
        self.assertEqual(self.mod.PLUGIN_VERSION, "0.3.0")

    def test_version_is_stamped_into_panel_and_state(self):
        html = self.mod._load_panel_html()
        self.assertIn(f"v{self.mod.PLUGIN_VERSION}", html)
        self.assertNotIn("__PLUGIN_VERSION__", html)
        self.assertIn('id="pluginVersion"', html)
        cap = self.cap_cls()
        cap.panel = FakePanel()
        cap._post_state({})
        self.assertEqual(cap.panel.posted[0]["version"], self.mod.PLUGIN_VERSION)

    # ---- no event loop / no sockets bound by the plugin itself
    def test_plugin_source_has_no_asyncio(self):
        import ast
        tree = ast.parse(PLUGIN_PATH.read_text(encoding="utf-8"))
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                self.assertNotIn("asyncio", [a.name.split(".")[0] for a in node.names])
            if isinstance(node, ast.ImportFrom):
                self.assertNotEqual((node.module or "").split(".")[0], "asyncio")
            self.assertNotIsInstance(node, (ast.AsyncFunctionDef, ast.Await, ast.AsyncWith, ast.AsyncFor))
            if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name):
                self.assertNotEqual(node.value.id, "asyncio")
        self.assertFalse(asyncio.iscoroutinefunction(self.cap_cls.execute))

    def test_permissions_are_declared_once_and_minimal(self):
        calls = []
        self.fake.request_permissions = lambda *a, **kw: calls.append((a, kw))
        self.fake.register_capability = lambda cls: None
        self.mod.OrcaAIAgentPackage().register_capabilities()
        self.assertEqual(calls, [((), {"network": [self.mod.GEMINI_CHAT_URL, self.mod.GEMINI_MODELS_URL]})])
        for url in (self.mod.GEMINI_CHAT_URL, self.mod.GEMINI_MODELS_URL):
            self.assertTrue(url.startswith(self.mod.GEMINI_API_ORIGIN + "/"))
            self.assertNotIn("*", url)
            self.assertNotIn("?", url)  # fixed URLs: the host grants network access per URL

    # ---- base-class constructors (host rejects overriding __init__ without calling it)
    def test_capability_calls_base_init_first(self):
        events = self.fake.base_init_events
        events.clear()
        cap = self.cap_cls()  # the fake host raises TypeError if the base __init__ is skipped
        self.assertTrue(cap._host_base_init_done)
        # ScriptPluginCapabilityBase.__init__ ran before the subclass created its own state.
        self.assertEqual(events, [("ScriptPluginCapabilityBase", False)])
        self.assertIsNone(cap.panel)

    def test_host_harness_rejects_missing_super_call(self):
        base = self.fake.script.ScriptPluginCapabilityBase

        class Broken(base):
            def __init__(self):
                self.panel = None

        with self.assertRaisesRegex(TypeError, "must be called when overriding __init__"):
            Broken()

        class BrokenPackage(self.fake.base):
            def __init__(self):
                pass

        with self.assertRaisesRegex(TypeError, "must be called when overriding __init__"):
            BrokenPackage()

    def test_package_base_init(self):
        events = self.fake.base_init_events
        events.clear()
        package = self.mod.OrcaAIAgentPackage()
        self.assertTrue(package._host_base_init_done)
        self.assertEqual(events, [("base", False)])

    def test_registration_then_host_instantiation(self):
        registered = []
        self.fake.register_capability = registered.append
        self.fake.base_init_events.clear()
        package = self.mod.OrcaAIAgentPackage()
        package.register_capabilities()
        self.assertEqual(registered, [self.cap_cls])
        instance = registered[0]()  # what the host does after registration
        self.assertTrue(instance._host_base_init_done)
        self.assertEqual([e[0] for e in self.fake.base_init_events], ["base", "ScriptPluginCapabilityBase"])

    def test_every_overriding_subclass_calls_super_init_first(self):
        import ast
        tree = ast.parse(PLUGIN_PATH.read_text(encoding="utf-8"))
        checked = []
        for node in ast.walk(tree):
            if not isinstance(node, ast.ClassDef):
                continue
            base_names = {ast.unparse(b) for b in node.bases}
            if not base_names & {"_SCRIPT_BASE", "orca.base", "orca.script.ScriptPluginCapabilityBase"}:
                continue
            init = next((n for n in node.body if isinstance(n, ast.FunctionDef) and n.name == "__init__"), None)
            if init is None:
                continue
            body = [n for n in init.body if not (isinstance(n, ast.Expr) and isinstance(n.value, ast.Constant))]
            first = ast.unparse(body[0]) if body else ""
            self.assertTrue(first.startswith("super().__init__("), f"{node.name}.__init__ starts with: {first}")
            checked.append(node.name)
        self.assertIn("AIAgentCapability", checked)

    # ---- A4
    def test_missing_dunder_file(self):
        source = PLUGIN_PATH.read_text(encoding="utf-8")
        namespace = {"__name__": "nofile_plugin"}  # no __file__
        sys.modules["orca"] = make_fake_orca()
        exec(compile(source, "<string>", "exec"), namespace)
        self.assertIsNone(namespace["PANEL_FILE"])
        self.assertEqual(namespace["_load_panel_html"](), expected_html(namespace["PLUGIN_VERSION"]))

    def test_unreadable_panel_file_falls_back(self):
        old = self.mod.PANEL_FILE
        self.mod.PANEL_FILE = HERE / "ui" / "does-not-exist.html"
        try:
            self.assertEqual(self.mod._load_panel_html(), expected_html(self.mod.PLUGIN_VERSION))
        finally:
            self.mod.PANEL_FILE = old

    # ---- A1 / A7 / A8
    def test_realistic_limits(self):
        cap = self.cap_cls
        self.assertIsNone(norm(cap, "wall_loops", "1000"))
        self.assertEqual(norm(cap, "wall_loops", "3"), "3")
        self.assertIsNone(norm(cap, "top_shell_layers", "2000000000"))
        self.assertIsNone(norm(cap, "outer_wall_speed", "1e30"))
        fil = lambda k, v: norm(cap, k, v, category="filament")
        self.assertIsNone(fil("nozzle_temperature", "1500"))
        self.assertIsNone(fil("nozzle_temperature", "20"))
        self.assertEqual(fil("nozzle_temperature", "215"), "215")
        self.assertIsNone(fil("hot_plate_temp", "300"))
        self.assertIsNone(fil("filament_max_volumetric_speed", "1e20"))
        self.assertIsNone(fil("filament_retraction_length", "500"))

    def test_speed_shape_and_machine_cap(self):
        cap = self.cap_cls
        self.assertEqual(norm(cap, "outer_wall_speed", "120"), "120")
        self.assertIsNone(norm(cap, "outer_wall_speed", "nil"))  # not nullable
        self.assertIsNone(norm(cap, "outer_wall_speed", "120,100"))  # scalar current value
        vector_ctx = {**CONTEXT, "process": {**CONTEXT["process"], "outer_wall_speed": "100,80"}}
        self.assertEqual(norm(cap, "outer_wall_speed", "120,90", vector_ctx), "120,90")
        self.assertIsNone(norm(cap, "outer_wall_speed", "120", vector_ctx))  # wrong arity
        self.assertIsNone(norm(cap, "outer_wall_speed", "600"))  # above machine_max_speed (500)
        self.assertEqual(norm(cap, "outer_wall_speed", "500"), "500")

    def test_flow_ratio(self):
        cap = self.cap_cls
        self.assertIsNone(norm(cap, "filament_flow_ratio", "nil", category="filament"))
        self.assertIsNone(norm(cap, "filament_flow_ratio", "0", category="filament"))
        self.assertIsNone(norm(cap, "filament_flow_ratio", "1.8", category="filament"))
        self.assertEqual(norm(cap, "filament_flow_ratio", "0.95", category="filament"), "0.95")

    def test_layer_height(self):
        cap = self.cap_cls
        self.assertEqual(norm(cap, "layer_height", "0.05"), "0.05")  # below min_layer_height: valid
        self.assertEqual(norm(cap, "layer_height", "0.4"), "0.4")  # == nozzle
        self.assertIsNone(norm(cap, "layer_height", "0.5"))  # > nozzle
        self.assertIsNone(norm(cap, "layer_height", "0.001"))
        no_nozzle = {**CONTEXT, "printer": {"min_layer_height": "0.07"}}
        self.assertEqual(norm(self.cap_cls, "layer_height", "0.3", no_nozzle), "0.3")

    # ---- C cautions
    def test_cautions(self):
        cap = self.cap_cls
        params = cap._sanitize_parameters(
            [{"category": "process", "key": "layer_height", "value": "0.05", "reason": ""},
             {"category": "process", "key": "support_style", "value": "grid", "reason": ""},
             {"category": "process", "key": "initial_layer_speed", "value": "300", "reason": ""}],
            CONTEXT)
        cap._attach_cautions(params, CONTEXT)
        by_key = {p["key"]: p.get("caution", "") for p in params}
        self.assertIn("minimum layer height", by_key["layer_height"])
        self.assertIn("Large change", by_key["layer_height"])
        self.assertIn("Support is disabled", by_key["support_style"])
        self.assertIn("First-layer speed", by_key["initial_layer_speed"])

        fil = cap._sanitize_parameters(
            [{"category": "filament", "key": "nozzle_temperature", "value": "245", "reason": ""}], CONTEXT)
        cap._attach_cautions(fil, CONTEXT)
        self.assertIn("recommended range (190–230", fil[0]["caution"])

    # ---- A11
    def test_redaction(self):
        r = self.mod._redact_secret
        key = "AIzaSyD-1234567890abcdefghijKLMNOPQ"
        self.assertEqual(r(f"bad key {key}!", key), "bad key ***!")
        other = "custom-secret-value-123456"
        self.assertNotIn(other, r(f"x {other} y", other))
        self.assertNotIn("a/b+c", r("x a%2Fb%2Bc y", "a/b+c==longsecret"))
        self.assertNotIn("AQ.Ab8RN6Iexamplekeyexamplekey12", r("k=AQ.Ab8RN6Iexamplekeyexamplekey12"))
        self.assertNotIn("abcdef0123456789abcd", r("Authorization: Bearer abcdef0123456789abcd"))
        self.assertNotIn("zzzzzzzzzzzz", r("https://x/y?alt=json&key=zzzzzzzzzzzz"))
        self.assertNotIn("qqqqqqqqqq", r("x-goog-api-key: qqqqqqqqqq"))
        self.assertEqual(r("hello world", "short"), "hello world")
        cap = self.cap_cls()
        cap.panel = FakePanel()
        cap._post_error(RuntimeError(f"boom {key}"))
        self.assertNotIn(key, cap.panel.posted[0]["message"])

    # ---- A2
    def test_response_text_checks(self):
        rt = self.mod._response_text
        self.assertEqual(rt(completion('{"a":1}')), '{"a":1}')
        with self.assertRaisesRegex(RuntimeError, "output token limit"):
            rt(completion('{"reply": "tr', finish="length"))
        with self.assertRaisesRegex(RuntimeError, "safety filters"):
            rt(completion(finish="content_filter"))
        with self.assertRaisesRegex(RuntimeError, "no candidates"):
            rt({"choices": []})
        with self.assertRaisesRegex(RuntimeError, "no candidates"):
            rt({})
        with self.assertRaisesRegex(RuntimeError, "empty response"):
            rt(completion(""))
        with self.assertRaisesRegex(RuntimeError, "stopped before finishing"):
            rt(completion("x", finish="tool_calls"))
        parts = {"choices": [{"finish_reason": "stop", "message": {"content": [{"text": "a"}, {"text": "b"}]}}]}
        self.assertEqual(rt(parts), "ab")

    def test_payload(self):
        msgs = [{"role": "user", "content": "hi"}]
        full = self.mod._build_payload("gemini-3.8-flash", msgs, True)
        self.assertEqual(full["max_tokens"], self.mod.GEMINI_MAX_OUTPUT_TOKENS)
        self.assertGreaterEqual(full["max_tokens"], 8192)
        self.assertEqual(full["reasoning_effort"], "low")
        self.assertEqual(full["response_format"]["type"], "json_schema")
        self.assertIn("key", full["response_format"]["json_schema"]["schema"]["properties"]["parameters"]["items"]["properties"])
        minimal = self.mod._build_payload("gemini-3.8-flash", msgs, False)
        self.assertEqual(set(minimal), {"model", "messages", "response_format"})
        self.assertNotIn("reasoning_effort", self.mod._build_payload("other-model", msgs, True))

    def test_http_transport(self):
        import urllib.error
        import urllib.request
        from unittest import mock
        seen = {}

        class Resp:
            def __enter__(self): return self
            def __exit__(self, *a): return False
            def read(self): return b'{"choices": []}'

        def fake_urlopen(request, timeout=None, context=None):
            seen.update(url=request.full_url, auth=request.get_header("Authorization"),
                        body=request.data, timeout=timeout, context=context)
            return Resp()

        with mock.patch.object(urllib.request, "urlopen", fake_urlopen):
            data = self.mod._post_chat_completion("K" * 20, {"model": "m"}, 12)
        self.assertEqual(data, {"choices": []})
        self.assertEqual(seen["url"], self.mod.GEMINI_CHAT_URL)
        self.assertEqual(seen["auth"], "Bearer " + "K" * 20)
        self.assertEqual(seen["timeout"], 12)
        self.assertIsNotNone(seen["context"])

        def http_error(request, timeout=None, context=None):
            import io
            raise urllib.error.HTTPError(request.full_url, 429, "Too Many", {}, io.BytesIO(b'[{"error": {"message": "slow down"}}]'))
        with mock.patch.object(urllib.request, "urlopen", http_error):
            with self.assertRaises(self.mod.GeminiHTTPError) as ctx:
                self.mod._post_chat_completion("K" * 20, {}, 1)
        self.assertEqual((ctx.exception.code, ctx.exception.message), (429, "slow down"))

        def cert_error(request, timeout=None, context=None):
            raise urllib.error.URLError("[SSL: CERTIFICATE_VERIFY_FAILED] bad")
        with mock.patch.object(urllib.request, "urlopen", cert_error):
            with self.assertRaisesRegex(RuntimeError, "CA certificates"):
                self.mod._post_chat_completion("K" * 20, {}, 1)

    def test_ssl_context_is_usable(self):
        import ssl
        self.assertIsInstance(self.mod._ssl_context(), ssl.SSLContext)


class FlowHarness(unittest.TestCase):
    """Shared helpers (no tests of its own)."""

    GOOD = '{"reply": "Use 3 walls.", "parameters": [{"category": "process", "key": "wall_loops", "value": "3", "reason": "stronger"}]}'

    def setUp(self):
        self.mod, self.fake = load_plugin("orca_ai_agent_flow_test")
        self.gates = []

    def tearDown(self):
        for gate in self.gates:
            gate.set()

    def gate(self):
        event = threading.Event()
        self.gates.append(event)
        return event

    def install(self, poster):
        """Replace the blocking HTTP call; returns the list of payloads it received."""
        calls = []

        def fake(api_key, payload, timeout):
            calls.append(payload)
            return poster(api_key, payload, timeout)

        self.mod._post_chat_completion = fake
        return calls

    def open(self):
        cap = self.mod.AIAgentCapability()
        result = cap.execute()
        self.assertTrue(result.ok, result.message)
        return cap, self.fake.panels[-1]

    @staticmethod
    def posted(panel, type_, request_id=None):
        return [m for m in list(panel.posted)
                if m.get("type") == type_ and (request_id is None or m.get("request_id") == request_id)]

    def wait_for(self, panel, type_, request_id=None, timeout=5.0):
        self.assertTrue(wait_until(lambda: self.posted(panel, type_, request_id), timeout),
                        f"no {type_} for {request_id}; posted={panel.posted}")
        return self.posted(panel, type_, request_id)[-1]

    def chat(self, cap, request_id, **extra):
        cap.on_message({"action": "chat", "request_id": request_id, "prompt": "hi",
                        "api_key": "K" * 20, "model": "m", **extra})


class ThreadFlowTests(FlowHarness):
    # ---- lifecycle
    def test_execute_is_synchronous_and_opens_panel_with_version(self):
        cap = self.mod.AIAgentCapability()
        self.assertFalse(asyncio.iscoroutinefunction(cap.execute))
        result = cap.execute()
        self.assertTrue(result.ok)
        self.assertIn(f"v{self.mod.PLUGIN_VERSION}", result.message)
        panel = self.fake.panels[-1]
        self.assertIn(f"v{self.mod.PLUGIN_VERSION}", panel.kwargs["html"])
        self.assertNotIn("__PLUGIN_VERSION__", panel.kwargs["html"])
        self.assertEqual(panel.kwargs["dock"], "right")
        again = cap.execute()
        self.assertIn("already open", again.message)
        self.assertEqual(len(self.fake.panels), 1)

    def test_popup_window_fallback(self):
        host_ui = self.fake.host.ui
        created = []
        del host_ui.create_dock_panel
        host_ui.create_window = lambda **kw: created.append(kw) or FakePanel(**kw)
        cap = self.mod.AIAgentCapability()
        self.assertTrue(cap.execute().ok)
        self.assertEqual(len(created), 1)

    def test_failure_to_create_panel_is_reported_not_raised(self):
        def boom(**kw):
            raise RuntimeError("no ui")
        self.fake.host.ui.create_dock_panel = boom
        result = self.mod.AIAgentCapability().execute()
        self.assertFalse(result.ok)
        self.assertIn("no ui", result.message)

    def test_on_cancelled_closes_panel_and_does_not_latch(self):
        cap, panel = self.open()
        cap.on_cancelled()
        self.assertFalse(panel.is_open())
        result = cap.execute()  # a later run must work (no permanent "cancelled" state)
        self.assertTrue(result.ok)
        self.assertIsNot(self.fake.panels[-1], panel)
        cap.on_unload()
        self.assertFalse(self.fake.panels[-1].is_open())
        self.assertTrue(cap.execute().ok)

    def test_state_message_carries_version_and_context_warning_free(self):
        cap, panel = self.open()
        cap.on_message({"action": "ready"})
        state = self.wait_for(panel, "state")
        self.assertEqual(state["version"], self.mod.PLUGIN_VERSION)
        self.assertNotIn("warning", state)

    # ---- chat in worker threads
    def test_chat_runs_in_worker_threads_and_uses_ui_snapshot(self):
        threads = []

        def poster(k, p, t):
            threads.append(threading.current_thread())
            return completion(self.GOOD)
        calls = self.install(poster)
        cap, panel = self.open()
        self.chat(cap, "r1")
        response = self.wait_for(panel, "chat_response", "r1")
        self.assertEqual(self.posted(panel, "ack", "r1")[0]["action"], "chat")
        self.assertTrue(self.posted(panel, "thinking", "r1"))
        self.assertEqual(response["target"]["name"], "0.20mm Standard")  # from the UI-thread snapshot
        self.assertEqual(response["parameters"][0]["current_value"], "2")
        self.assertEqual(threads[0].name, "orca-ai-agent-http")
        self.assertIsNot(threads[0], threading.main_thread())
        self.assertIn("0.20mm Standard", calls[0]["messages"][-1]["content"])
        self.assertEqual({h["request_id"] for h in cap._history}, {"r1"})
        self.assertTrue(wait_until(lambda: cap._active is None))

    def test_history_roles_replayed(self):
        calls = self.install(lambda k, p, t: completion(self.GOOD))
        cap, panel = self.open()
        self.chat(cap, "a"); self.wait_for(panel, "chat_response", "a")
        self.assertTrue(wait_until(lambda: cap._active is None))
        self.chat(cap, "b"); self.wait_for(panel, "chat_response", "b")
        self.assertEqual([m["role"] for m in calls[1]["messages"]], ["system", "user", "assistant", "user"])

    def test_missing_key_and_invalid_requests(self):
        cap, panel = self.open()
        cap.on_message({"action": "chat", "request_id": "r1", "prompt": "hi", "api_key": "", "model": "m"})
        self.assertEqual(self.wait_for(panel, "error", "r1")["code"], "missing_api_key")
        cap.on_message({"action": "chat", "prompt": "hi"})  # no request id
        self.assertTrue(wait_until(lambda: any(m.get("code") == "invalid_request" for m in panel.posted)))
        cap.on_message({"action": "bogus", "request_id": "x"})
        self.assertIn("Unknown panel action", self.wait_for(panel, "error", "x")["message"])

    def test_errors_never_contain_the_api_key(self):
        key = "AIzaSy" + "y" * 30

        def boom(k, p, t):
            raise RuntimeError("upstream said key=" + key)
        self.install(boom)
        cap, panel = self.open()
        cap.on_message({"action": "chat", "request_id": "r1", "prompt": "hi", "api_key": key, "model": "m"})
        error = self.wait_for(panel, "error", "r1")
        self.assertNotIn(key, error["message"])
        self.assertNotIn("yyyyyyyy", error["message"])
        self.assertTrue(wait_until(lambda: cap._active is None))

    # ---- generate: payload, errors, timeouts
    def generate(self, cap, request_id="r"):
        return cap._generate_reply("p", "k" * 20, "gemini-3.8-flash", CONTEXT, self.mod._ChatRequest(request_id))

    def test_generate_payload(self):
        calls = self.install(lambda k, p, t: completion(self.GOOD))
        reply, params, target, warning = self.generate(self.mod.AIAgentCapability())
        self.assertEqual(params[0]["value"], "3")
        self.assertEqual(calls[0]["max_tokens"], self.mod.GEMINI_MAX_OUTPUT_TOKENS)
        self.assertEqual(calls[0]["messages"][0]["role"], "system")

    def test_truncation_and_http_errors_are_clear(self):
        self.install(lambda k, p, t: completion('{"reply": "x', finish="length"))
        with self.assertRaisesRegex(RuntimeError, "output token limit"):
            self.generate(self.mod.AIAgentCapability())

        def err(k, p, t):
            raise self.mod.GeminiHTTPError(429, "quota")
        self.install(err)
        with self.assertRaisesRegex(RuntimeError, "quota or rate limit"):
            self.generate(self.mod.AIAgentCapability())

    def test_400_retries_once_without_optional_fields(self):
        def poster(k, p, t):
            if "max_tokens" in p:
                raise self.mod.GeminiHTTPError(400, "Unknown field max_tokens")
            return completion(self.GOOD)
        calls = self.install(poster)
        self.generate(self.mod.AIAgentCapability())
        self.assertEqual(len(calls), 2)
        self.assertNotIn("max_tokens", calls[1])
        self.assertEqual(calls[1]["response_format"], {"type": "json_object"})

    def test_rejected_key_is_not_retried(self):
        def poster(k, p, t):
            raise self.mod.GeminiHTTPError(400, "API key not valid. Please pass a valid API key.")
        calls = self.install(poster)
        with self.assertRaisesRegex(RuntimeError, "rejected the request"):
            self.generate(self.mod.AIAgentCapability())
        self.assertEqual(len(calls), 1)

    def test_deadline_times_out_and_leaves_no_history(self):
        gate = self.gate()
        self.install(lambda k, p, t: gate.wait(10))
        self.mod.GEMINI_TOTAL_TIMEOUT_SECONDS = 0.3
        cap, panel = self.open()
        self.chat(cap, "r1")
        error = self.wait_for(panel, "error", "r1")
        self.assertIn("did not respond in time", error["message"])
        self.assertEqual(cap._history, [])
        self.assertTrue(wait_until(lambda: cap._active is None))

    # ---- cancel / close / timeout sync (A5)
    def test_cancel_in_flight_then_new_request(self):
        gate = self.gate()
        started = threading.Event()
        count = []

        def poster(k, p, t):
            count.append(1)
            if len(count) == 1:
                started.set()
                gate.wait(10)  # the abandoned first request finishes late
            return completion(self.GOOD)
        self.install(poster)
        cap, panel = self.open()
        self.chat(cap, "r1")
        self.assertTrue(started.wait(5))
        self.chat(cap, "r2")  # busy
        self.assertIn("already in progress", self.wait_for(panel, "error", "r2")["message"])
        cap.on_message({"action": "cancel", "request_id": "r1"})
        self.assertTrue(self.wait_for(panel, "cancelled", "r1")["cancelled"])
        self.chat(cap, "r3")  # accepted at once, although r1's socket thread is still blocked
        self.wait_for(panel, "chat_response", "r3")
        gate.set()
        time.sleep(0.4)
        self.assertEqual(self.posted(panel, "chat_response", "r1"), [])  # late result is dropped
        self.assertEqual({h["request_id"] for h in cap._history}, {"r3"})
        # a late cancel (the panel timed out after the answer was recorded) rolls the history back
        cap.on_message({"action": "cancel", "request_id": "r3"})
        self.assertTrue(wait_until(lambda: any(m.get("history_rolled_back") for m in self.posted(panel, "cancelled", "r3"))))
        self.assertEqual(cap._history, [])

    def test_closing_the_panel_cancels_the_request(self):
        gate = self.gate()
        started = threading.Event()
        self.install(lambda k, p, t: (started.set(), gate.wait(10), completion(self.GOOD))[2])
        cap, panel = self.open()
        self.chat(cap, "r1")
        self.assertTrue(started.wait(5))
        request = cap._active
        cap.on_close()
        self.assertTrue(request.cancel.is_set())
        self.assertIsNone(cap._active)
        gate.set()
        time.sleep(0.3)
        self.assertEqual(self.posted(panel, "chat_response"), [])
        self.assertEqual(cap._history, [])

    def test_clear_history(self):
        self.install(lambda k, p, t: completion(self.GOOD))
        cap, panel = self.open()
        self.chat(cap, "a"); self.wait_for(panel, "chat_response", "a")
        self.assertTrue(wait_until(lambda: cap._active is None))
        cap.on_message({"action": "clear_history", "request_id": "c1"})
        self.wait_for(panel, "history_cleared", "c1")
        self.assertEqual(cap._history, [])

    # ---- apply runs on the calling (UI) thread, not in a worker
    def test_apply_commits_on_the_ui_thread(self):
        self.install(lambda k, p, t: completion(self.GOOD))
        cap, panel = self.open()
        self.chat(cap, "r1")
        response = self.wait_for(panel, "chat_response", "r1")
        self.assertTrue(wait_until(lambda: cap._active is None))
        cap.on_message({"action": "apply", "request_id": "ap1",
                        "parameters": response["parameters"], "target": response["target"]})
        result = self.wait_for(panel, "apply_result", "ap1")
        self.assertEqual(result["keys"], ["wall_loops"])
        commit = self.fake.commits[-1]
        self.assertEqual(commit["staged"], {"wall_loops": "3"})
        self.assertEqual(commit["expected"], {"wall_loops": "2"})
        self.assertIs(commit["thread"], threading.current_thread())

    # ---- the Windows problem: no event loop, no socket.bind anywhere in the flow
    def test_full_flow_never_creates_an_event_loop_or_binds_a_socket(self):
        import urllib.error
        import urllib.request
        from unittest import mock

        class Resp:
            def __enter__(self): return self
            def __exit__(self, *a): return False
            def read(self): return ('{"choices": [{"finish_reason": "stop", "message": {"content": %s}}]}'
                                    % __import__("json").dumps(self_good)).encode()
        self_good = self.GOOD

        def forbidden(name):
            def raiser(*a, **k):
                raise AssertionError(f"{name} must not be called by the plugin")
            return raiser

        AUDIT_STATE["events"].clear()
        AUDIT_STATE["enabled"] = True
        try:
            with mock.patch.object(asyncio, "new_event_loop", forbidden("asyncio.new_event_loop")), \
                 mock.patch.object(asyncio, "get_event_loop", forbidden("asyncio.get_event_loop")), \
                 mock.patch.object(asyncio, "run", forbidden("asyncio.run")), \
                 mock.patch.object(socket, "socketpair", forbidden("socket.socketpair")), \
                 mock.patch.object(socket.socket, "bind", forbidden("socket.bind")), \
                 mock.patch.object(urllib.request, "urlopen", lambda *a, **k: Resp()):
                cap, panel = self.open()
                cap.on_message({"action": "ready"})
                self.wait_for(panel, "state")
                self.chat(cap, "r1")  # real _post_chat_completion, only urlopen is faked
                response = self.wait_for(panel, "chat_response", "r1")
                self.assertTrue(wait_until(lambda: cap._active is None))
                cap.on_message({"action": "apply", "request_id": "ap",
                                "parameters": response["parameters"], "target": response["target"]})
                self.wait_for(panel, "apply_result", "ap")
                self.chat(cap, "r2")
                cap.on_message({"action": "cancel", "request_id": "r2"})
                cap.on_message({"action": "clear_history", "request_id": "c"})
                cap.on_close()
                cap.on_cancelled()
        finally:
            AUDIT_STATE["enabled"] = False
        self.assertEqual(AUDIT_STATE["events"], [])
        self.assertFalse([m for m in panel.posted if m.get("type") == "error" and "must not be called" in m.get("message", "")])



COMPAT_MODELS = {"object": "list", "data": [
    {"id": "models/gemini-2.5-flash"}, {"id": "models/gemini-2.5-pro"}, {"id": "models/gemini-3.8-flash"},
    {"id": "models/gemini-2.5-flash-lite"}, {"id": "models/gemini-3.8-flash-preview-09-2025"},
    {"id": "models/text-embedding-004"}, {"id": "models/imagen-4.0-generate"}, {"id": "models/gemini-2.5-flash-image"},
    {"id": "models/gemini-2.5-flash-preview-tts"}, {"id": "models/gemini-live-2.5-flash"}, {"id": "models/gemma-3-27b-it"},
    {"id": "models/veo-3.0-generate"}, {"id": "models/aqa"}, {"id": "models/gemini-2.5-flash"},
    {"id": "weird name!"}, {"id": "models/other-llm"}, {"id": ""}, 42,
]}
EXPECTED_MODELS = ["gemini-3.8-flash", "gemini-3.8-flash-preview-09-2025", "gemini-2.5-flash",
                   "gemini-2.5-pro", "gemini-2.5-flash-lite", "gemma-3-27b-it"]


class RetryTests(FlowHarness):
    """Automatic retries for transient errors (HTTP 503 and friends)."""

    def setUp(self):
        super().setUp()
        self.mod.RETRY_BASE_DELAY_SECONDS = 0.01

    def counting(self, behaviour):
        attempts = []

        def poster(k, p, t):
            attempts.append(time.monotonic())
            return behaviour(len(attempts))
        self.install(poster)
        return attempts

    def generate(self, cap=None, request_id="r"):
        cap = cap or self.mod.AIAgentCapability()
        return cap._generate_reply("p", "k" * 20, "m", CONTEXT, self.mod._ChatRequest(request_id))

    def test_503_succeeds_on_second_retry(self):
        def behaviour(n):
            if n < 3:
                raise self.mod.GeminiHTTPError(503, "The model is overloaded. Please try again later.")
            return completion(self.GOOD)
        attempts = self.counting(behaviour)
        cap, panel = self.open()
        self.chat(cap, "r1")
        self.wait_for(panel, "chat_response", "r1")
        self.assertEqual(len(attempts), 3)  # first try + 2 retries
        notices = [m["message"] for m in self.posted(panel, "thinking", "r1") if "Retrying" in m["message"]]
        self.assertEqual(len(notices), 2)
        self.assertIn("attempt 1/3", notices[0])
        self.assertIn("attempt 2/3", notices[1])
        self.assertEqual(self.posted(panel, "error"), [])
        self.assertEqual({h["request_id"] for h in cap._history}, {"r1"})

    def test_persistent_503_reports_a_clear_message_after_all_retries(self):
        attempts = self.counting(lambda n: (_ for _ in ()).throw(self.mod.GeminiHTTPError(503, "overloaded")))
        cap, panel = self.open()
        self.chat(cap, "r1")
        message = self.wait_for(panel, "error", "r1")["message"]
        self.assertEqual(len(attempts), 1 + self.mod.MAX_TRANSIENT_RETRIES)
        self.assertIn("temporary surge", message)
        self.assertIn("HTTP 503", message)
        self.assertIn("3 automatic retries", message)
        self.assertIn("try again", message.lower())
        self.assertIn("different model", message)
        self.assertEqual(self.posted(panel, "chat_response"), [])
        self.assertEqual(cap._history, [])
        self.assertTrue(wait_until(lambda: cap._active is None))

    def test_503_respects_cancel_during_backoff(self):
        self.mod.RETRY_BASE_DELAY_SECONDS = 30  # a long backoff that only a cancel can cut short
        first = threading.Event()

        def behaviour(n):
            first.set()
            raise self.mod.GeminiHTTPError(503, "overloaded")
        attempts = self.counting(behaviour)
        cap, panel = self.open()
        self.chat(cap, "r1")
        self.assertTrue(first.wait(5))
        started = time.monotonic()
        cap.on_message({"action": "cancel", "request_id": "r1"})
        self.assertTrue(self.wait_for(panel, "cancelled", "r1")["cancelled"])
        worker_gone = wait_until(lambda: not any(
            th.name == "orca-ai-agent-chat" and th.is_alive() for th in threading.enumerate()), 3)
        self.assertTrue(worker_gone, "worker thread kept sleeping through the backoff")
        self.assertLess(time.monotonic() - started, 3)
        self.assertEqual(len(attempts), 1)  # no further attempt after the cancel
        self.assertEqual(self.posted(panel, "error"), [])
        self.assertEqual(self.posted(panel, "chat_response"), [])
        self.assertEqual(cap._history, [])

    def test_503_respects_cancel_between_attempts(self):
        self.mod.RETRY_BASE_DELAY_SECONDS = 0.3
        cap_holder = {}

        def behaviour(n):
            if n == 2:  # cancel while the second attempt is "in flight"
                cap_holder["cap"].on_message({"action": "cancel", "request_id": "r1"})
            raise self.mod.GeminiHTTPError(503, "overloaded")
        attempts = self.counting(behaviour)
        cap, panel = self.open()
        cap_holder["cap"] = cap
        self.chat(cap, "r1")
        self.wait_for(panel, "cancelled", "r1")
        self.assertTrue(wait_until(lambda: not any(
            th.name == "orca-ai-agent-chat" and th.is_alive() for th in threading.enumerate()), 3))
        self.assertEqual(len(attempts), 2)
        self.assertEqual(self.posted(panel, "error"), [])

    def test_503_respects_the_total_deadline(self):
        self.mod.GEMINI_TOTAL_TIMEOUT_SECONDS = 0.5
        self.mod.RETRY_BASE_DELAY_SECONDS = 5  # the next backoff cannot fit before the deadline
        attempts = self.counting(lambda n: (_ for _ in ()).throw(self.mod.GeminiHTTPError(503, "overloaded")))
        started = time.monotonic()
        with self.assertRaisesRegex(RuntimeError, "temporary surge"):
            self.generate()
        self.assertLess(time.monotonic() - started, 2)
        self.assertEqual(len(attempts), 1)

    def test_retry_helper_honours_cancel_and_deadline(self):
        mod = self.mod
        calls = []

        def attempt():
            calls.append(1)
            raise mod.GeminiHTTPError(503, "")
        mod.RETRY_BASE_DELAY_SECONDS = 10
        with self.assertRaises(mod.GeminiHTTPError) as ctx:
            mod._retry_transient(attempt, threading.Event(), time.monotonic() + 1)
        self.assertEqual((len(calls), ctx.exception.retries), (1, 0))
        cancelled = threading.Event()
        cancelled.set()
        with self.assertRaises(mod.RequestCancelled):
            mod._retry_transient(attempt, cancelled, time.monotonic() + 100)
        later = threading.Event()
        threading.Timer(0.1, later.set).start()
        started = time.monotonic()
        with self.assertRaises(mod.RequestCancelled):
            mod._retry_transient(attempt, later, time.monotonic() + 100)
        self.assertLess(time.monotonic() - started, 2)

    def test_non_transient_errors_are_never_retried(self):
        for code, detail in ((400, "Bad request"), (401, "no"), (403, "forbidden"), (404, "no model"), (429, "quota")):
            attempts = self.counting(lambda n, c=code, d=detail: (_ for _ in ()).throw(self.mod.GeminiHTTPError(c, d)))
            with self.assertRaises(RuntimeError):
                self.generate()
            expected = 2 if code == 400 else 1  # 400 retries once without the optional fields
            self.assertEqual(len(attempts), expected, f"HTTP {code}")

    def test_other_server_errors_are_retried_and_described(self):
        for code in (500, 502, 504):
            attempts = self.counting(lambda n, c=code: (_ for _ in ()).throw(self.mod.GeminiHTTPError(c, "")))
            with self.assertRaisesRegex(RuntimeError, rf"temporary server problem \(HTTP {code}\).*3 automatic retries"):
                self.generate()
            self.assertEqual(len(attempts), 4)

    def test_transient_network_errors_are_retried(self):
        def behaviour(n):
            if n < 3:
                raise self.mod.TransientNetworkError("Could not reach Gemini: timed out")
            return completion(self.GOOD)
        attempts = self.counting(behaviour)
        reply, params, target, warning = self.generate()
        self.assertEqual(len(attempts), 3)
        attempts = self.counting(lambda n: (_ for _ in ()).throw(self.mod.TransientNetworkError("Could not reach Gemini: reset")))
        with self.assertRaisesRegex(RuntimeError, "3 automatic retries.*Check your connection"):
            self.generate()

    def test_network_error_classification(self):
        import urllib.error
        import urllib.request
        from unittest import mock

        def raising(exc):
            def fake(request, timeout=None, context=None):
                raise exc
            return fake
        cases = [
            (urllib.error.URLError(ConnectionResetError("reset")), self.mod.TransientNetworkError),
            (urllib.error.URLError(TimeoutError("timed out")), self.mod.TransientNetworkError),
            (ConnectionResetError("reset while reading"), self.mod.TransientNetworkError),
            (TimeoutError("read timed out"), self.mod.TransientNetworkError),
        ]
        for exc, expected in cases:
            with mock.patch.object(urllib.request, "urlopen", raising(exc)):
                with self.assertRaises(expected):
                    self.mod._post_chat_completion("K" * 20, {}, 1)
        permanent = urllib.error.URLError("[Errno -2] Name or service not known")
        with mock.patch.object(urllib.request, "urlopen", raising(permanent)):
            with self.assertRaises(RuntimeError) as ctx:
                self.mod._post_chat_completion("K" * 20, {}, 1)
        self.assertNotIsInstance(ctx.exception, self.mod.TransientNetworkError)

    def test_backoff_is_exponential_with_bounded_jitter(self):
        from unittest import mock
        mod, _ = load_plugin("orca_ai_agent_backoff")
        self.assertEqual(mod.MAX_TRANSIENT_RETRIES, 3)
        for index, base in enumerate((1.0, 2.0, 4.0)):
            with mock.patch.object(mod.random, "uniform", lambda lo, hi: lo):
                self.assertAlmostEqual(mod._backoff_delay(index), base)
            with mock.patch.object(mod.random, "uniform", lambda lo, hi: hi):
                self.assertAlmostEqual(mod._backoff_delay(index), base * (1 + mod.RETRY_JITTER_FRACTION))
            for _ in range(50):
                self.assertTrue(base <= mod._backoff_delay(index) <= base * (1 + mod.RETRY_JITTER_FRACTION) + 1e-9)


class ModelFetchTests(FlowHarness):
    """The fetch_models action: list the provider's models for the typed API key."""

    KEY = "custom-secret-key-1234567890"

    def install_models(self, behaviour):
        calls = []

        def fake(api_key, timeout):
            calls.append((api_key, timeout))
            return behaviour(api_key, timeout)
        self.mod._get_models = fake
        return calls

    def fetch(self, cap, request_id="m1", key=None):
        cap.on_message({"action": "fetch_models", "request_id": request_id,
                        "api_key": self.KEY if key is None else key})

    def test_model_filter_and_order(self):
        self.assertEqual(self.mod._model_names(COMPAT_MODELS), EXPECTED_MODELS)
        native = {"models": [
            {"name": "models/gemini-2.5-pro", "supportedGenerationMethods": ["generateContent", "countTokens"]},
            {"name": "models/gemini-2.5-flash", "supportedGenerationMethods": ["generateContent"]},
            {"name": "models/gemini-embedding-001", "supportedGenerationMethods": ["embedContent"]},
            {"name": "models/gemini-2.0-flash-exp-no-methods-field"},
        ]}
        self.assertEqual(self.mod._model_names(native),
                         ["gemini-2.5-flash", "gemini-2.5-pro", "gemini-2.0-flash-exp-no-methods-field"])
        self.assertEqual(self.mod._model_names({}), [])
        self.assertEqual(self.mod._model_names({"data": "nope"}), [])
        many = {"data": [{"id": f"gemini-{i}.0-flash"} for i in range(1, 300)]}
        self.assertEqual(len(self.mod._model_names(many)), self.mod.MAX_MODELS)

    def test_fetch_success(self):
        calls = self.install_models(lambda k, t: COMPAT_MODELS)
        cap, panel = self.open()
        self.fetch(cap)
        message = self.wait_for(panel, "models", "m1")
        self.assertEqual(message["models"], EXPECTED_MODELS)
        self.assertEqual(message["default_model"], self.mod.DEFAULT_MODEL)
        self.assertEqual(self.posted(panel, "ack", "m1")[0]["action"], "fetch_models")
        self.assertEqual(calls[0][0], self.KEY)
        self.assertLessEqual(calls[0][1], self.mod.MODELS_HTTP_TIMEOUT_SECONDS)
        self.assertTrue(wait_until(lambda: cap._active is None))
        self.assertEqual(cap._history, [])  # fetching a list never touches the chat history

    def test_real_transport_sends_a_get_with_the_key_in_the_header_only(self):
        import urllib.request
        from unittest import mock
        seen = {}

        class Resp:
            def __enter__(self): return self
            def __exit__(self, *a): return False
            def read(self): return __import__("json").dumps(COMPAT_MODELS).encode()

        def fake_urlopen(request, timeout=None, context=None):
            seen.update(url=request.full_url, method=request.get_method(), data=request.data,
                        auth=request.get_header("Authorization"), timeout=timeout)
            return Resp()
        with mock.patch.object(urllib.request, "urlopen", fake_urlopen):
            cap, panel = self.open()
            self.fetch(cap)
            self.assertEqual(self.wait_for(panel, "models", "m1")["models"], EXPECTED_MODELS)
        self.assertEqual(seen["url"], self.mod.GEMINI_MODELS_URL)
        self.assertEqual(seen["method"], "GET")
        self.assertIsNone(seen["data"])
        self.assertEqual(seen["auth"], "Bearer " + self.KEY)
        self.assertNotIn(self.KEY, seen["url"])
        self.assertNotIn(self.KEY, repr(panel.posted))

    def test_authentication_errors(self):
        for code, detail, expected in (
            (400, "API key not valid. Please pass a valid API key.", "rejected the request"),
            (401, "", "rejected the API key"),
            (403, "Permission denied", "not allowed"),
        ):
            self.setUp()
            calls = self.install_models(lambda k, t, c=code, d=detail: (_ for _ in ()).throw(self.mod.GeminiHTTPError(c, d)))
            cap, panel = self.open()
            self.fetch(cap, f"m{code}")
            error = self.wait_for(panel, "error", f"m{code}")
            self.assertIn(expected, error["message"])
            self.assertEqual(len(calls), 1, "auth errors are not retried")
            self.assertEqual(self.posted(panel, "models"), [])
            self.assertTrue(wait_until(lambda: cap._active is None))

    def test_network_errors_and_timeout(self):
        def unreachable(k, t):
            raise RuntimeError("Could not reach Gemini: [Errno -2] Name or service not known")
        self.install_models(unreachable)
        cap, panel = self.open()
        self.fetch(cap)
        self.assertIn("Could not reach Gemini", self.wait_for(panel, "error", "m1")["message"])

        self.setUp()
        gate = self.gate()
        self.install_models(lambda k, t: gate.wait(10))
        self.mod.MODELS_TOTAL_TIMEOUT_SECONDS = 0.3
        cap, panel = self.open()
        self.fetch(cap)
        self.assertIn("did not respond in time", self.wait_for(panel, "error", "m1")["message"])

    def test_transient_errors_are_retried_for_the_model_list(self):
        self.mod.RETRY_BASE_DELAY_SECONDS = 0.01
        count = []

        def behaviour(k, t):
            count.append(1)
            if len(count) < 3:
                raise self.mod.GeminiHTTPError(503, "overloaded")
            return COMPAT_MODELS
        self.install_models(behaviour)
        cap, panel = self.open()
        self.fetch(cap)
        self.assertEqual(self.wait_for(panel, "models", "m1")["models"], EXPECTED_MODELS)
        self.assertEqual(len(count), 3)

        self.setUp()
        self.mod.RETRY_BASE_DELAY_SECONDS = 0.01
        self.install_models(lambda k, t: (_ for _ in ()).throw(self.mod.GeminiHTTPError(503, "overloaded")))
        cap, panel = self.open()
        self.fetch(cap)
        self.assertIn("temporary surge", self.wait_for(panel, "error", "m1")["message"])

    def test_api_key_is_redacted_everywhere(self):
        key = self.KEY
        leaks = [
            self.mod.GeminiHTTPError(400, f"API key {key} not valid"),
            RuntimeError(f"upstream failure for key={key} and url ?key={key}"),
            RuntimeError("Bearer " + key + " was rejected"),
        ]
        for leak in leaks:
            self.setUp()
            self.install_models(lambda k, t, e=leak: (_ for _ in ()).throw(e))
            cap, panel = self.open()
            self.fetch(cap)
            self.wait_for(panel, "error", "m1")
            self.assertNotIn(key, repr(panel.posted))
            self.assertTrue(wait_until(lambda: cap._active is None))
        # a key shaped like a Google key is caught by pattern even when it is not the active key
        self.setUp()
        stray = "AIza" + "Z" * 35
        self.install_models(lambda k, t: (_ for _ in ()).throw(RuntimeError("echo " + stray)))
        cap, panel = self.open()
        self.fetch(cap)
        self.assertNotIn(stray, repr(self.wait_for(panel, "error", "m1")))

    def test_missing_or_oversized_key(self):
        calls = self.install_models(lambda k, t: COMPAT_MODELS)
        cap, panel = self.open()
        self.fetch(cap, key="   ")
        self.assertEqual(self.wait_for(panel, "error", "m1")["code"], "missing_api_key")
        self.assertTrue(wait_until(lambda: cap._active is None))
        self.fetch(cap, "m2", key="K" * (self.mod.MAX_API_KEY_LENGTH + 1))
        self.assertIn("unexpectedly long", self.wait_for(panel, "error", "m2")["message"])
        self.assertEqual(calls, [])

    def test_cancel_during_fetch_drops_the_result(self):
        gate = self.gate()
        started = threading.Event()

        def behaviour(k, t):
            started.set()
            gate.wait(10)
            return COMPAT_MODELS
        self.install_models(behaviour)
        cap, panel = self.open()
        self.fetch(cap)
        self.assertTrue(started.wait(5))
        cap.on_message({"action": "cancel", "request_id": "m1"})
        self.assertTrue(self.wait_for(panel, "cancelled", "m1")["cancelled"])
        gate.set()
        time.sleep(0.3)
        self.assertEqual(self.posted(panel, "models"), [])
        self.assertEqual(self.posted(panel, "error"), [])
        self.install_models(lambda k, t: COMPAT_MODELS)  # a new fetch is accepted immediately
        self.fetch(cap, "m2")
        self.wait_for(panel, "models", "m2")

    def test_empty_result_is_reported(self):
        self.install_models(lambda k, t: {"data": [{"id": "models/text-embedding-004"}]})
        cap, panel = self.open()
        self.fetch(cap)
        message = self.wait_for(panel, "error", "m1")["message"]
        self.assertIn("no chat-capable models", message)
        self.assertIn("type a model name manually", message)

    def test_single_flight_with_chat(self):
        gate = self.gate()
        started = threading.Event()
        self.install_models(lambda k, t: (started.set(), gate.wait(10), COMPAT_MODELS)[2])
        self.install(lambda k, p, t: completion(FlowHarness.GOOD))
        cap, panel = self.open()
        self.fetch(cap)
        self.assertTrue(started.wait(5))
        self.chat(cap, "c1")
        self.assertIn("already in progress", self.wait_for(panel, "error", "c1")["message"])
        gate.set()
        self.wait_for(panel, "models", "m1")
        self.assertTrue(wait_until(lambda: cap._active is None))
        # and the other way round
        gate2 = self.gate()
        began = threading.Event()
        self.install(lambda k, p, t: (began.set(), gate2.wait(10), completion(FlowHarness.GOOD))[2])
        self.chat(cap, "c2")
        self.assertTrue(began.wait(5))
        self.fetch(cap, "m3")
        self.assertIn("already in progress", self.wait_for(panel, "error", "m3")["message"])

    def test_fetch_flow_creates_no_event_loop_and_binds_no_socket(self):
        import urllib.request
        from unittest import mock
        import contextlib

        class Resp:
            def __enter__(self): return self
            def __exit__(self, *a): return False
            def read(self): return __import__("json").dumps(COMPAT_MODELS).encode()

        def forbidden(name):
            def raiser(*a, **k):
                raise AssertionError(f"{name} must not be called by the plugin")
            return raiser
        AUDIT_STATE["events"].clear()
        AUDIT_STATE["enabled"] = True
        try:
            with contextlib.ExitStack() as stack:
                for target, attr in ((asyncio, "new_event_loop"), (asyncio, "get_event_loop"), (asyncio, "run"),
                                     (socket, "socketpair"), (socket.socket, "bind")):
                    stack.enter_context(mock.patch.object(target, attr, forbidden(attr)))
                stack.enter_context(mock.patch.object(urllib.request, "urlopen", lambda *a, **k: Resp()))
                cap, panel = self.open()
                self.fetch(cap)
                self.wait_for(panel, "models", "m1")
        finally:
            AUDIT_STATE["enabled"] = False
        self.assertEqual(AUDIT_STATE["events"], [])


# --------------------------------------------------------------------------- the panel's real JavaScript
HARNESS_JS = r"""
const fs = require("fs");
const vm = require("vm");
const html = fs.readFileSync(process.argv[2], "utf8");
const script = html.match(/<script>([\s\S]*)<\/script>/)[1];
const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(m => m[1]);
const checks = [];
const check = (name, ok, detail) => checks.push({ name, ok: !!ok, detail: ok ? "" : String(detail) });

class El {
  constructor(id) { this.id = id; this.children = []; this.listeners = {}; this.value = ""; this.textContent = "";
    this.className = ""; this.hidden = false; this.disabled = false; this.checked = false; this.type = "text";
    this.style = {}; this.scrollTop = 0; this.scrollHeight = 0; this.focused = false; }
  addEventListener(type, fn) { (this.listeners[type] = this.listeners[type] || []).push(fn); }
  fire(type, extra) { const e = Object.assign({ type, preventDefault() {} }, extra || {});
    (this.listeners[type] || []).forEach(fn => fn.call(this, e)); return e; }
  dispatchEvent(e) { (this.listeners[e.type] || []).forEach(fn => fn.call(this, e)); return true; }
  appendChild(c) { this.children.push(c); return c; }
  replaceChildren(...cs) { this.children = cs; }
  focus() { this.focused = true; }
  get options() { return this.children; }
}
class Event { constructor(type) { this.type = type; } }

function boot(storage) {
  const els = {};
  ids.forEach(id => { els[id] = new El(id); });
  els.model.value = "gemini-3.8-flash";
  // Honour attributes that the real markup sets, such as the initially hidden dropdown.
  ids.forEach(id => {
    const tag = html.match(new RegExp("<[a-z]+[^>]*\\bid=\"" + id + "\"[^>]*>"));
    if (tag && /\shidden(\s|>|=)/.test(tag[0])) { els[id].hidden = true; }
  });
  const posted = [];
  let handler = null;
  const timers = [];
  let now = 0;
  const win = {
    localStorage: { getItem: k => (k in storage ? storage[k] : null), setItem: (k, v) => { storage[k] = String(v); },
                    removeItem: k => { delete storage[k]; } },
    orca: { postMessage: m => posted.push(JSON.parse(JSON.stringify(m))), onMessage: fn => { handler = fn; } },
    setTimeout: (fn, ms) => { timers.push({ fn, at: now + ms, id: timers.length + 1, live: true }); return timers.length; },
    clearTimeout: id => { if (timers[id - 1]) timers[id - 1].live = false; },
  };
  const doc = { getElementById: id => els[id], createElement: () => new El(null) };
  vm.runInNewContext(script, { window: win, document: doc, Event, Date });
  return {
    els, posted, storage,
    deliver: m => handler(m),
    last: action => posted.filter(m => m.action === action).slice(-1)[0],
    advance: ms => { now += ms; timers.filter(t => t.live && t.at <= now).forEach(t => { t.live = false; t.fn(); }); },
  };
}

const KEY = "K".repeat(20);
const MODELS = ["gemini-3.8-flash", "gemini-2.5-pro", "gemma-3-27b-it"];

// 1. fetch success: loading state, datalist + dropdown, manual typing, persistence
{
  const ui = boot({});
  check("ready is sent on boot", ui.last("ready"));
  check("dropdown hidden before fetching", ui.els.modelSelect.hidden === true);
  check("model input is bound to the datalist", /list="modelList"/.test(html) && /<datalist id="modelList">/.test(html));
  ui.els.apiKey.value = KEY;
  ui.els.fetchModels.fire("click");
  const request = ui.last("fetch_models");
  check("fetch_models is posted with the key and a request id", request && request.api_key === KEY && request.request_id);
  check("loading status shown", /busy/.test(ui.els.modelStatus.className) && /Fetching/.test(ui.els.modelStatus.textContent));
  check("button turns into Cancel while loading", ui.els.fetchModels.textContent === "Cancel" && !ui.els.fetchModels.disabled);
  check("other controls locked while loading", ui.els.model.disabled && ui.els.send.disabled);
  ui.deliver({ type: "ack", action: "fetch_models", request_id: request.request_id });
  ui.deliver({ type: "models", models: [...MODELS, "bad name!", "gemini-2.5-pro", 7], request_id: request.request_id, default_model: "x" });
  check("datalist filled with valid unique names", ui.els.modelList.children.length === 3, ui.els.modelList.children.length);
  check("dropdown has placeholder + models and is visible", ui.els.modelSelect.children.length === 4 && ui.els.modelSelect.hidden === false);
  check("success status", /3 models loaded/.test(ui.els.modelStatus.textContent) && /ready/.test(ui.els.modelStatus.className));
  check("controls unlocked and button restored", ui.els.fetchModels.textContent === "Fetch Models" && !ui.els.model.disabled && !ui.els.fetchModels.disabled);
  check("list cached for the next open", JSON.parse(ui.storage["orca.ai_agent.models"]).length === 3);
  ui.els.modelSelect.value = "gemini-2.5-pro";
  ui.els.modelSelect.fire("change");
  check("dropdown choice fills the input and is saved", ui.els.model.value === "gemini-2.5-pro" && ui.storage["orca.ai_agent.model"] === "gemini-2.5-pro");
  ui.els.model.value = "my-custom-model";
  ui.els.model.fire("input");
  check("typing a custom name is allowed and clears the dropdown", ui.els.modelSelect.value === "");
  ui.els.model.fire("change");
  check("custom name is saved", ui.storage["orca.ai_agent.model"] === "my-custom-model");
  ui.els.prompt.value = "hello";
  ui.els.send.fire("click");
  check("chat uses the manually typed model", ui.last("chat").model === "my-custom-model");
}

// 2. errors keep manual entry possible
{
  const ui = boot({});
  ui.els.apiKey.value = KEY;
  ui.els.fetchModels.fire("click");
  const id = ui.last("fetch_models").request_id;
  ui.deliver({ type: "error", request_id: id, message: "Gemini rejected the API key." });
  check("error shown next to the model field", /error/.test(ui.els.modelStatus.className) && /rejected the API key/.test(ui.els.modelStatus.textContent));
  check("controls usable again after an error", !ui.els.model.disabled && !ui.els.fetchModels.disabled && ui.els.fetchModels.textContent === "Fetch Models");
  check("dropdown stays hidden after an error", ui.els.modelSelect.hidden === true);
  ui.els.model.value = "typed-by-hand";
  ui.els.model.fire("change");
  check("manual model still saved after an error", ui.storage["orca.ai_agent.model"] === "typed-by-hand");
}

// 3. missing key never reaches the host
{
  const ui = boot({});
  ui.els.fetchModels.fire("click");
  check("no request without a key", !ui.last("fetch_models"));
  check("missing key explained", /API key first/.test(ui.els.modelStatus.textContent) && ui.els.apiKey.focused);
}

// 4. cancel while loading, late answers ignored
{
  const ui = boot({});
  ui.els.apiKey.value = KEY;
  ui.els.fetchModels.fire("click");
  const id = ui.last("fetch_models").request_id;
  ui.els.fetchModels.fire("click");
  const cancel = ui.last("cancel");
  check("second click cancels the same request", cancel && cancel.request_id === id);
  check("UI is idle again after cancel", ui.els.fetchModels.textContent === "Fetch Models" && !ui.els.model.disabled);
  ui.deliver({ type: "models", models: MODELS, request_id: id });
  check("a late list for a cancelled request is ignored", ui.els.modelSelect.hidden === true && ui.els.modelList.children.length === 0);
}

// 5. timeout sends a cancel and unlocks the UI
{
  const ui = boot({});
  ui.els.apiKey.value = KEY;
  ui.els.fetchModels.fire("click");
  const id = ui.last("fetch_models").request_id;
  ui.deliver({ type: "ack", action: "fetch_models", request_id: id });
  ui.advance(36000);
  check("timeout cancels on the backend", ui.last("cancel") && ui.last("cancel").request_id === id);
  check("timeout explained and manual entry kept", /timed out/.test(ui.els.modelStatus.textContent) && !ui.els.model.disabled);
}

// 6. cached list restored on open, invalid cache ignored
{
  const ui = boot({ "orca.ai_agent.models": JSON.stringify(["gemini-2.5-flash", "bad name!"]) });
  check("cached list restored", ui.els.modelSelect.hidden === false && ui.els.modelList.children.length === 1);
  const broken = boot({ "orca.ai_agent.models": "{not json" });
  check("broken cache is ignored", broken.els.modelSelect.hidden === true);
}

// 7. version label, retry notice, prompt retention, IME
{
  const ui = boot({});
  ui.deliver({ type: "state", version: "9.9.9", default_model: "x", history: [] });
  check("version label follows the backend", ui.els.pluginVersion.textContent === "v9.9.9");
  ui.deliver({ type: "state", version: "<script>", history: [] });
  check("unsafe version strings are rejected", ui.els.pluginVersion.textContent === "v9.9.9");

  ui.els.apiKey.value = KEY;
  ui.els.prompt.value = "use 3 walls";
  ui.els.prompt.fire("keydown", { key: "Enter", shiftKey: false, isComposing: true, keyCode: 13 });
  check("Enter during IME composition does not send", !ui.last("chat"));
  ui.els.prompt.fire("keydown", { key: "Enter", shiftKey: false, isComposing: false, keyCode: 229 });
  check("keyCode 229 does not send", !ui.last("chat"));
  ui.els.prompt.fire("keydown", { key: "Enter", shiftKey: false, isComposing: false, keyCode: 13 });
  const chat = ui.last("chat");
  check("plain Enter sends", chat && chat.prompt === "use 3 walls");
  check("prompt text is kept while waiting", ui.els.prompt.value === "use 3 walls");
  ui.deliver({ type: "ack", action: "chat", request_id: chat.request_id });
  ui.deliver({ type: "thinking", message: "Gemini is busy. Retrying in 1 s (attempt 1/3)…", request_id: chat.request_id });
  check("retry notice is shown to the user", /Retrying in 1 s/.test(ui.els.thinkingText.textContent));
  ui.deliver({ type: "error", request_id: chat.request_id, message: "Gemini's servers are experiencing a temporary surge in requests (HTTP 503)." });
  check("prompt kept after a failed request", ui.els.prompt.value === "use 3 walls");
  ui.els.send.fire("click");
  const again = ui.last("chat");
  ui.deliver({ type: "chat_response", reply: "ok", parameters: [], request_id: again.request_id });
  check("prompt cleared only after the answer", ui.els.prompt.value === "");
}

// 8. API key never rendered
{
  const ui = boot({});
  ui.els.apiKey.value = KEY;
  ui.els.fetchModels.fire("click");
  const id = ui.last("fetch_models").request_id;
  ui.deliver({ type: "error", request_id: id, message: "bad key " + KEY + " here" });
  check("API key redacted in the model status", ui.els.modelStatus.textContent.indexOf(KEY) === -1, ui.els.modelStatus.textContent);
}

console.log(JSON.stringify(checks));
"""


@unittest.skipUnless(__import__("shutil").which("node"), "node is not installed")
class PanelBehaviourTests(unittest.TestCase):
    """Runs the panel's real script against a minimal fake DOM and host bridge in Node."""

    def test_panel_script_behaviour(self):
        import json
        import subprocess
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            harness = Path(tmp) / "harness.js"
            harness.write_text(HARNESS_JS, encoding="utf-8")
            done = subprocess.run(["node", str(harness), str(PANEL_PATH)], capture_output=True, text=True, timeout=60)
        self.assertEqual(done.returncode, 0, done.stderr)
        checks = json.loads(done.stdout.strip().splitlines()[-1])
        self.assertGreater(len(checks), 30)
        failed = [f"{c['name']}: {c['detail']}" for c in checks if not c["ok"]]
        self.assertEqual(failed, [])


class PanelStaticTests(unittest.TestCase):
    def setUp(self):
        self.html = PANEL_PATH.read_text(encoding="utf-8")

    def test_panel_features(self):
        self.assertIn('http-equiv="Content-Security-Policy"', self.html)
        self.assertIn("event.isComposing", self.html)
        self.assertIn("event.keyCode !== 229", self.html)
        self.assertIn('send("cancel"', self.html)
        self.assertIn('id="cancelRequest"', self.html)
        self.assertIn('id="pluginVersion"', self.html)
        self.assertIn("v__PLUGIN_VERSION__", self.html)
        self.assertIn("message.version", self.html)
        for needle in ('id="fetchModels"', 'list="modelList"', '<datalist id="modelList">', 'id="modelSelect"',
                       'id="modelStatus"', 'startRequest("fetch_models"', 'case "models":', "FETCH_MODELS_TIMEOUT_MS"):
            self.assertIn(needle, self.html)
        self.assertIn("redactSecrets(text)", self.html)
        # prompt is cleared only when the answer arrives
        before_submit_end = self.html.split("function submitPrompt()")[1].split("elements.send.addEventListener")[0]
        self.assertNotIn('elements.prompt.value = ""', before_submit_end)

    def test_embedded_blob_is_current(self):
        sys.path.insert(0, str(HERE))
        import embed_panel
        self.assertEqual(embed_panel.decode_blob(PLUGIN_PATH.read_text(encoding="utf-8")),
                         PANEL_PATH.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
