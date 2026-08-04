#!/usr/bin/env python3
"""studio_server — the reference fusion-binding dev-server (C++ <-> viz seam).

A tiny stdlib HTTP server that turns the `studio_bridge` CLI into a local endpoint
the browser visualizer can fetch REAL demon-core physics from — the drop-in for
viz/js/simstub.js's fake evaluate/generateRun, with zero new C++ deps and no
Emscripten. It shells to the bridge per request (each runs a real Monte-Carlo
eigen / burst, seconds — so the UI must debounce; the viz-seam note says as much).

    POST /evaluate       body = cfg JSON  ->  EvaluateResult JSON (the gauge)
    POST /generate-run   body = cfg JSON  ->  the run JSON (tally + run + samples)
    GET  /health         -> {"ok": true}

Usage (from the repo root, after a build):
    python tools/studio_server.py [--bridge <path>] [--port 8100]

The DATA is mechanism-independent: a WASM build or a native/Electron addon (the
Steam path, M7) wrap the same evaluate_json/generate_run_json surface. The JS
adapter that replaces simstub.js and reconstructs the presentation closures
(flux(t)/compression(t)) from this data is the viz/ track's job.
"""

import argparse
import json
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

# Windows path rule (machine-wide): forward slashes work everywhere.
_DEFAULT_BRIDGE = "build/win-x64/Release/studio_bridge.exe"


def _find_bridge(explicit: str | None) -> Path:
    candidates = [explicit] if explicit else [
        _DEFAULT_BRIDGE,
        "build/win-x64/Release/studio_bridge",   # non-Windows
        "build/Release/studio_bridge",
    ]
    for c in candidates:
        if c and Path(c).exists():
            return Path(c)
    sys.exit(f"studio_bridge not found (looked in {candidates}); build it first, "
             f"or pass --bridge <path>")


class Handler(BaseHTTPRequestHandler):
    bridge: Path = Path(_DEFAULT_BRIDGE)

    def _send(self, code: int, payload: bytes, ctype: str = "application/json") -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Access-Control-Allow-Origin", "*")  # dev-only: the viz is a separate origin
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self) -> None:
        if self.path == "/health":
            self._send(200, b'{"ok": true}')
        else:
            self._send(404, b'{"error": "GET only /health"}')

    def do_POST(self) -> None:
        sub = {"/evaluate": "evaluate", "/generate-run": "generate-run"}.get(self.path)
        if sub is None:
            self._send(404, b'{"error": "POST /evaluate or /generate-run"}')
            return
        n = int(self.headers.get("Content-Length", "0"))
        cfg = self.rfile.read(n)
        try:
            # Each call runs a real MC solve (seconds). Shell to the bridge.
            out = subprocess.run([str(self.bridge), sub], input=cfg,
                                 capture_output=True, check=True)
            self._send(200, out.stdout)
        except subprocess.CalledProcessError as e:
            self._send(400, json.dumps({"error": e.stderr.decode("utf-8", "replace")}).encode())

    def log_message(self, fmt: str, *args) -> None:  # quieter default logging
        sys.stderr.write("studio_server: " + (fmt % args) + "\n")


def main() -> None:
    ap = argparse.ArgumentParser(description="fusion-binding dev-server over studio_bridge")
    ap.add_argument("--bridge", default=None, help="path to the studio_bridge executable")
    ap.add_argument("--port", type=int, default=8100)
    args = ap.parse_args()

    Handler.bridge = _find_bridge(args.bridge)
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"studio_server: {Handler.bridge} on http://127.0.0.1:{args.port} "
          f"(POST /evaluate, /generate-run)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.shutdown()


if __name__ == "__main__":
    main()
