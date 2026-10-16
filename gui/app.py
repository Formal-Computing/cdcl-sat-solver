#!/usr/bin/env python3
"""Browser-based educational CDCL-MOMS GUI backed by the real C++ executable.

No nonstandard Python packages required. Runs a local-only HTTP server.
"""
from __future__ import annotations

import argparse
import errno
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import tempfile
import webbrowser
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent.parent
HTML = Path(__file__).resolve().parent / "index.html"
EXAMPLES = ROOT / "examples"
MAX_INPUT = 256 * 1024
SOLVER_TIMEOUT = 20


def solver_path() -> Path:
    for candidate in (ROOT / "build/sat_solver", ROOT / "build/sat_solver.exe"):
        if candidate.is_file():
            return candidate
    raise RuntimeError("Solver binary missing: run cmake --build build first.")


def solve_text(source: str) -> dict:
    if not isinstance(source, str) or not source.strip():
        raise ValueError("Please supply a DIMACS CNF formula.")
    if len(source.encode("utf-8")) > MAX_INPUT:
        raise ValueError("This educational GUI accepts at most 256 KiB of CNF.")
    with tempfile.TemporaryDirectory(prefix="cdcl-gui-") as directory:
        path = Path(directory) / "input.cnf"
        path.write_text(source, encoding="utf-8")
        try:
            process = subprocess.run(
                [str(solver_path()), "--events-json", str(path)],
                capture_output=True, text=True,
                timeout=SOLVER_TIMEOUT, check=False,
            )
        except subprocess.TimeoutExpired as exc:
            raise ValueError("Solver exceeded the 20-second GUI time limit.") from exc
    if process.returncode not in (10, 20):
        raise ValueError(process.stderr.strip() or "C++ solver could not read this CNF.")
    try:
        result = json.loads(process.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError("C++ solver produced invalid JSON event data.") from exc
    if result["status"] != ("SAT" if process.returncode == 10 else "UNSAT"):
        raise RuntimeError("C++ solver result disagrees with process exit code.")
    if len(result["events"]) > 5000:
        raise ValueError("Too many steps for GUI replay; use the terminal for large benchmarks.")
    return result


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format_string: str, *args: object) -> None:
        # Avoid logging pasted formula contents, show only request path.
        super().log_message(format_string, *args)

    def response(self, status: int, body: bytes, mimetype: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", mimetype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def json(self, status: int, payload: object) -> None:
        self.response(status, json.dumps(payload).encode(), "application/json; charset=utf-8")

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        if parsed.path == "/":
            self.response(200, HTML.read_bytes(), "text/html; charset=utf-8")
            return
        if parsed.path == "/api/examples":
            self.json(200, {"examples": sorted(path.name for path in EXAMPLES.glob("*.cnf"))})
            return
        if parsed.path == "/api/example":
            name = parse_qs(parsed.query).get("name", [""])[0]
            if name not in {x.name for x in EXAMPLES.glob("*.cnf")}:
                self.json(404, {"error": "Unknown example."})
                return
            self.json(200, {"cnf": (EXAMPLES / name).read_text(encoding="utf-8")})
            return
        if parsed.path == "/health":
            self.json(200, {"ok": True, "project": "CDCL-MOMS"})
            return
        self.json(404, {"error": "Not found."})

    def do_POST(self) -> None:
        if self.path != "/api/solve":
            self.json(404, {"error": "Not found."})
            return
        try:
            size = int(self.headers.get("Content-Length", "0"))
            if not 0 < size <= MAX_INPUT * 2:
                raise ValueError("Invalid or oversized request.")
            payload = json.loads(self.rfile.read(size))
            result = solve_text(payload.get("cnf", ""))
        except (ValueError, json.JSONDecodeError) as exc:
            self.json(400, {"error": str(exc)})
            return
        except Exception as exc:
            self.json(500, {"error": str(exc)})
            return
        self.json(200, result)



def create_server(port: int | None, *, max_port: int = 8785) -> ThreadingHTTPServer:
    """Bind a local GUI server.

    An explicitly supplied port is strict. With no --port argument, start
    at 8765 and use the first free port through max_port. --port 0 asks
    the operating system to pick any available local port.
    """
    if port is not None:
        return ThreadingHTTPServer(("127.0.0.1", port), Handler)

    for candidate in range(8765, max_port + 1):
        try:
            return ThreadingHTTPServer(("127.0.0.1", candidate), Handler)
        except OSError as exc:
            if exc.errno != errno.EADDRINUSE:
                raise
    raise OSError(errno.EADDRINUSE,
                  "All GUI ports 8765–" + str(max_port) +
                  " are in use; try --port 0 for an automatically assigned port.")


def main() -> None:
    parser = argparse.ArgumentParser(description="Run the CDCL-MOMS educational visualizer")
    parser.add_argument("--port", type=int, default=None,
                        help="Local port. Default: first free from 8765–8785; 0 = any free port.")
    parser.add_argument("--no-browser", action="store_true")
    options = parser.parse_args()
    if options.port is not None and not 0 <= options.port <= 65535:
        parser.error("--port must be between 0 and 65535")
    solver_path()  # fail early with a useful build instruction
    try:
        server = create_server(options.port)
    except OSError as exc:
        if exc.errno == errno.EADDRINUSE:
            parser.exit(2, f"Port unavailable: {exc}. Choose another with --port 0 or --port 8767.\\n")
        raise
    url = f"http://127.0.0.1:{server.server_port}"
    if options.port is None and server.server_port != 8765:
        print(f"Port 8765 is occupied; using free port {server.server_port}.", flush=True)
    print(f"CDCL-MOMS visualizer: {url}", flush=True)
    print("Ctrl+C to stop. GUI executes the C++ binary; Python does not solve SAT.", flush=True)
    if not options.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
