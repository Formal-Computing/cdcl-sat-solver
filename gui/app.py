#!/usr/bin/env python3
"""Browser-based educational CDCL-MOMS GUI backed by the real C++ executable.

No nonstandard Python packages required. Runs a local-only HTTP server.
"""
from __future__ import annotations

import argparse
import errno
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import re
import shutil
import sys
import threading
from pathlib import Path
import subprocess
import tempfile
import time
import webbrowser
from urllib.parse import parse_qs, urlparse
from benchmark_catalog import list_catalog, fetch_benchmark

ROOT = Path(__file__).resolve().parent.parent
HTML = Path(__file__).resolve().parent / "index.html"
EXAMPLES = ROOT / "examples"
MAX_INPUT = 256 * 1024
SMT_INPUT_LIMIT = 2 * 1024 * 1024
_SMT_RUNNING: dict[str, subprocess.Popen] = {}
_SMT_LOCK = threading.Lock()


def z3_python() -> Path | None:
    """Choose the Python with z3-solver without assuming it is globally installed."""
    candidates = [os.environ.get("CDCL_SMT_PYTHON", ""),
                  str(Path.home() / ".venvs/cdcl-smt/bin/python"), sys.executable]
    for candidate in candidates:
        if not candidate or not Path(candidate).is_file():
            continue
        result = subprocess.run([candidate, "-c", "import z3"], capture_output=True)
        if result.returncode == 0:
            return Path(candidate)
    return None


def check_smt_text(source: str, run_id: str | None = None) -> dict:
    """Single query only. Full Z3 SMT semantics; no time cap or fake CDCL trace."""
    if not isinstance(source, str) or not source.strip():
        raise ValueError("Please supply an SMT-LIB 2 script.")
    if len(source.encode("utf8")) > SMT_INPUT_LIMIT:
        raise ValueError("SMT input exceeds 2 MiB.")
    # Remove line comments for simple check-sat command cardinality validation.
    clean = "\n".join(x.split(";", 1)[0] for x in source.splitlines())
    number = len(re.findall(r"\(\s*check-sat(?:\s*\)|\s)", clean))
    if number != 1 or re.search(r"\(\s*(?:push|pop|check-sat-assuming)\b", clean):
        raise ValueError("Only non-incremental SMT scripts with exactly one check-sat are supported.")
    python = z3_python()
    if python is None:
        raise RuntimeError("Z3 reference backend missing. Install z3-solver in a virtual environment.")
    started = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix="cdcl-smt-") as folder:
        path = Path(folder) / "benchmark.smt2"
        path.write_text(source, encoding="utf8")
        process = subprocess.Popen(
            [str(python), str(ROOT / "gui/smt_worker.py"), str(path)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )  # No deadline; reference checks may be manually cancelled.
        if run_id is not None:
            if not re.fullmatch(r"[0-9a-f]{32}", run_id):
                process.terminate(); process.wait()
                raise ValueError("Invalid solve run ID")
            with _SMT_LOCK:
                if run_id in _SMT_RUNNING:
                    process.terminate(); process.wait()
                    raise ValueError("Duplicate solve ID")
                _SMT_RUNNING[run_id] = process
        try:
            stdout, stderr = process.communicate()
        finally:
            if run_id is not None:
                with _SMT_LOCK:
                    _SMT_RUNNING.pop(run_id, None)
    elapsed = time.perf_counter() - started
    if process.returncode is not None and process.returncode < 0:
        raise ValueError("Reference solver cancelled or interrupted")
    try:
        response = json.loads(stdout.strip())
    except json.JSONDecodeError:
        raise RuntimeError("Reference solver returned invalid output: " + stderr[-200:])
    if process.returncode or "error" in response:
        raise ValueError(response.get("error", stderr[-200:]))
    response["process_elapsed_seconds"] = round(elapsed, 6)
    return response


def check_native_qflia_text(source: str, run_id: str | None = None) -> dict:
    """Run the actual C++ CDCL(T) QF_LIA engine; never fall back to Z3."""
    if not isinstance(source, str) or not source.strip():
        raise ValueError("Please supply a QF_LIA SMT-LIB script.")
    if len(source.encode("utf-8")) > SMT_INPUT_LIMIT:
        raise ValueError("SMT input exceeds 2 MiB.")
    if run_id is not None and not re.fullmatch(r"[0-9a-f]{32}", run_id):
        raise ValueError("Invalid solve run ID")
    started = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix="cdcl-native-qflia-") as directory:
        path = Path(directory) / "formula.smt2"
        path.write_text(source, encoding="utf-8")
        process = subprocess.Popen(
            [str(solver_path()), "--smt-json", str(path)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        if run_id is not None:
            with _SMT_LOCK:
                if run_id in _SMT_RUNNING:
                    process.terminate()
                    process.wait()
                    raise ValueError("Duplicate solve ID")
                _SMT_RUNNING[run_id] = process
        try:
            stdout, stderr = process.communicate()  # no wall-clock timeout
        finally:
            if run_id is not None:
                with _SMT_LOCK:
                    _SMT_RUNNING.pop(run_id, None)
    if process.returncode is not None and process.returncode < 0:
        raise ValueError("Native solver cancelled or interrupted")
    if process.returncode not in (0, 10, 20):
        raise RuntimeError("Native solver failed: " + stderr[-400:])
    try:
        result = json.loads(stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError("Native C++ SMT solver produced invalid JSON") from exc
    if result.get("status") not in ("SAT", "UNSAT", "UNKNOWN"):
        raise RuntimeError("Unexpected C++ SMT solver status")
    expected = {"SAT": 10, "UNSAT": 20, "UNKNOWN": 0}[result["status"]]
    if process.returncode != expected:
        raise RuntimeError("C++ SMT status and exit code disagree")
    result["elapsed_seconds"] = round(time.perf_counter() - started, 6)
    return result


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
        # No wall-clock deadline. Hard SATLIB instances may take minutes or
        # longer; record the real elapsed time instead of cancelling at 20s.
        started = time.perf_counter()
        process = subprocess.run(
            [str(solver_path()), "--events-json", str(path)],
            capture_output=True, text=True, check=False,
        )
        elapsed_seconds = time.perf_counter() - started
    if process.returncode not in (10, 20):
        raise ValueError(process.stderr.strip() or "C++ solver could not read this CNF.")
    try:
        result = json.loads(process.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError("C++ solver produced invalid JSON event data.") from exc
    if result["status"] != ("SAT" if process.returncode == 10 else "UNSAT"):
        raise RuntimeError("C++ solver result disagrees with process exit code.")
    # Time covers the C++ process (startup, DIMACS parsing, solving and
    # JSON serialization), not browser rendering or fetching the response.
    result["elapsed_seconds"] = round(elapsed_seconds, 6)
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
        if parsed.path == "/api/theory-examples":
            theory = EXAMPLES / "smt"
            self.json(200, {"examples": sorted(x.name for x in theory.glob("*.smt2"))})
            return
        if parsed.path == "/api/theory-example":
            theory = EXAMPLES / "smt"
            name = parse_qs(parsed.query).get("name", [""])[0]
            if name not in {x.name for x in theory.glob("*.smt2")}:
                self.json(404, {"error": "Unknown theory example."})
                return
            self.json(200, {"name": name,
                            "source": (theory / name).read_text(encoding="utf-8"),
                            "runnable": name.startswith(("lia_", "qf_lia_"))})
            return
        if parsed.path == "/api/example":
            name = parse_qs(parsed.query).get("name", [""])[0]
            if name not in {x.name for x in EXAMPLES.glob("*.cnf")}:
                self.json(404, {"error": "Unknown example."})
                return
            self.json(200, {"cnf": (EXAMPLES / name).read_text(encoding="utf-8")})
            return
        if parsed.path == "/api/benchmark-catalog":
            query = parse_qs(parsed.query)
            try:
                payload = list_catalog(
                    source=query.get("source", ["2020"])[0],
                    logic=query.get("logic", [""])[0],
                    q=query.get("q", [""])[0][:120],
                    limit=int(query.get("limit", ["80"])[0]),
                    offset=int(query.get("offset", ["0"])[0]),
                    ready_only=query.get("ready_only", ["0"])[0] == "1")
                self.json(200, payload)
            except (ValueError, OSError) as exc:
                self.json(400, {"error": str(exc)})
            return
        if parsed.path == "/api/benchmark":
            identifier = parse_qs(parsed.query).get("id", [""])[0]
            try:
                self.json(200, fetch_benchmark(identifier))
            except ValueError as exc:
                self.json(400, {"error": str(exc)})
            except LookupError as exc:
                self.json(409, {"error": str(exc)})
            return
        if parsed.path == "/api/benchmark-status":
            self.json(200, {"z3_reference_available": z3_python() is not None,
                            "native_smt_supported": True,
                            "native_logic": "QF_LIA",
                            "native_backend": "C++ CDCL(T) with exact integer theory"})
            return
        if parsed.path == "/health":
            self.json(200, {"ok": True, "project": "CDCL-MOMS"})
            return
        self.json(404, {"error": "Not found."})

    def stream_solver(self) -> None:
        """Forward the actual C++ solver's events to the browser immediately.

        NDJSON is deliberately used instead of WebSockets: stdlib HTTP is
        sufficient, results are inspectable, and no solve deadline is imposed.
        A dropped browser connection terminates only its own child process.
        """
        try:
            size = int(self.headers.get("Content-Length", "0"))
            if not 0 < size <= MAX_INPUT * 2:
                raise ValueError("Invalid or oversized request.")
            payload = json.loads(self.rfile.read(size))
            source = payload.get("cnf", "")
            if not isinstance(source, str) or not source.strip():
                raise ValueError("Please supply a DIMACS CNF formula.")
            if len(source.encode("utf-8")) > MAX_INPUT:
                raise ValueError("This educational GUI accepts at most 256 KiB of CNF.")
            binary = solver_path()
        except (ValueError, json.JSONDecodeError, RuntimeError) as exc:
            self.json(400, {"error": str(exc)})
            return

        with tempfile.TemporaryDirectory(prefix="cdcl-live-") as directory:
            path = Path(directory) / "input.cnf"
            path.write_text(source, encoding="utf-8")
            started = time.perf_counter()
            with tempfile.TemporaryFile(mode="w+t", encoding="utf-8") as errors:
                with subprocess.Popen(
                    [str(binary), "--events-ndjson", str(path)],
                    stdout=subprocess.PIPE, stderr=errors,
                    text=True, bufsize=1,
                ) as process:
                    self.send_response(200)
                    self.send_header("Content-Type", "application/x-ndjson; charset=utf-8")
                    self.send_header("Cache-Control", "no-store, no-transform")
                    self.send_header("X-Content-Type-Options", "nosniff")
                    self.send_header("Connection", "close")
                    self.end_headers()
                    try:
                        assert process.stdout is not None
                        for line in process.stdout:
                            self.wfile.write(line.encode("utf-8"))
                            self.wfile.flush()
                        return_code = process.wait()  # no timeout
                        elapsed = time.perf_counter() - started
                        if return_code not in (10, 20):
                            errors.seek(0)
                            message = errors.read(4096).strip() or "Solver failed."
                            self.wfile.write((json.dumps({"kind": "error", "error": message}) + "\n").encode())
                        else:
                            self.wfile.write((json.dumps({"kind": "timing", "elapsed_seconds": round(elapsed, 6)}) + "\n").encode())
                        self.wfile.flush()
                    except (BrokenPipeError, ConnectionResetError):
                        # A user navigated away or closed the tab.
                        if process.poll() is None:
                            process.terminate()
                            process.wait()
                    finally:
                        if process.stdout:
                            process.stdout.close()

    def do_POST(self) -> None:
        if self.path == "/api/native-smt":
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= SMT_INPUT_LIMIT * 2:
                    raise ValueError("Invalid QF_LIA request length")
                payload = json.loads(self.rfile.read(size))
                response = check_native_qflia_text(payload.get("source", ""),
                                                   payload.get("run_id"))
                self.json(200, response)
            except (ValueError, json.JSONDecodeError) as exc:
                self.json(400, {"error": str(exc)})
            except RuntimeError as exc:
                self.json(500, {"error": str(exc)})
            return
        if self.path == "/api/smt-check":
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= SMT_INPUT_LIMIT * 2:
                    raise ValueError("Invalid SMT request length")
                payload = json.loads(self.rfile.read(size))
                response = check_smt_text(payload.get("source", ""), payload.get("run_id"))
                self.json(200, response)
            except (ValueError, json.JSONDecodeError) as exc:
                self.json(400, {"error": str(exc)})
            except RuntimeError as exc:
                self.json(503, {"error": str(exc)})
            return
        if self.path == "/api/smt-cancel":
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if length < 1 or length > 256:
                    raise ValueError("Invalid cancellation request")
                run_id = json.loads(self.rfile.read(length)).get("run_id", "")
                if not isinstance(run_id, str) or not re.fullmatch(r"[0-9a-f]{32}", run_id):
                    raise ValueError("Invalid run ID")
                with _SMT_LOCK:
                    running = _SMT_RUNNING.get(run_id)
                    if running is not None and running.poll() is None:
                        running.terminate()
                self.json(200, {"cancel_requested": running is not None})
            except (ValueError, json.JSONDecodeError) as exc:
                self.json(400, {"error": str(exc)})
            return
        if self.path == "/api/stream":
            self.stream_solver()
            return
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
