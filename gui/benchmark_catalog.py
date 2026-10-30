"""Strictly indexed, lazily downloaded SMT-LIB benchmark catalog.

SMT-COMP 2020's selection contains *paths*, not SMT-LIB file bodies.
A separately labeled 2025 mirror provides a verified per-file raw URL
for a limited subset. Never claim its content is the frozen 2020 release.
"""
from __future__ import annotations
import gzip
import hashlib
import json
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
CATALOG = HERE / "catalog"
CACHE = HERE / ".benchmark-cache"
RAW_BASE = "https://raw.githubusercontent.com/rjungemann/smt-lib-benchmarks/main/"
MAX_FILE = 2 * 1024 * 1024
_CACHE = None


def catalog():
    global _CACHE
    if _CACHE is not None:
        return _CACHE
    with gzip.open(CATALOG / "smtcomp2020_selected.json.gz", "rt", encoding="utf8") as f:
        originals = json.load(f)
    mirrors = json.loads((CATALOG / "mirror2025.json").read_text("utf8"))
    if len(originals) != len(set(originals)):
        raise ValueError("Duplicate competition paths")
    source_map = {}
    for name in mirrors:
        if not name.startswith("smtlib-2025/") or not name.endswith(".smt2"):
            continue
        parts = name.split("/", 2)
        if len(parts) != 3 or parts[1] not in {
            "QF_IDL", "QF_RDL", "QF_LIA", "QF_LRA",
            "QF_UF", "QF_UFIDL", "QF_UFLIA", "QF_UFLRA"}:
            continue
        original = parts[2].replace("__", "/")
        source_map[original] = name
    entries = {"2020": [], "mirror": []}
    lookup = {}
    for path in originals:
        identifier = hashlib.sha256(("2020:" + path).encode()).hexdigest()[:24]
        mirror = source_map.get(path)
        record = {"id": identifier, "path": path,
                  "name": path.rsplit("/", 1)[-1],
                  "logic": path.split("/")[1],
                  "ready": mirror is not None,
                  "source": "2020",
                  "provenance": "SMT-COMP 2020 selected path" }
        entries["2020"].append(record)
        lookup[identifier] = (record, mirror)
    for original, mirror in source_map.items():
        identifier = hashlib.sha256(("mirror:" + mirror).encode()).hexdigest()[:24]
        record = {"id": identifier, "path": original, "name": original.rsplit("/", 1)[-1],
                  "logic": original.split("/")[1], "ready": True,
                  "source": "mirror", "provenance": "SMT-LIB 2025 bounded mirror"}
        entries["mirror"].append(record)
        lookup[identifier] = (record, mirror)
    # Include 2025-only samples in the mirror tab, while keeping them out
    # of the historical 2020 selection.
    all_mirrors = set(source_map.values())
    for mirror in mirrors:
        if mirror in all_mirrors or not mirror.startswith("smtlib-2025/"):
            continue
        parts = mirror.split("/", 2)
        if len(parts) != 3:
            continue
        orig = parts[2].replace("__", "/")
        if not orig.startswith("non-incremental/"):
            continue
        identifier = hashlib.sha256(("mirror:" + mirror).encode()).hexdigest()[:24]
        record = {"id": identifier, "path": orig, "name": orig.rsplit("/", 1)[-1],
                  "logic": parts[1], "ready": True,
                  "source": "mirror", "provenance": "SMT-LIB 2025 bounded mirror"}
        entries["mirror"].append(record)
        lookup[identifier] = (record, mirror)
    _CACHE = entries, lookup
    return _CACHE


def list_catalog(source: str, logic: str = "", q: str = "",
                 limit: int = 80, offset: int = 0, ready_only: bool = False) -> dict:
    entries, lookup = catalog()
    if source not in entries:
        raise ValueError("Unknown benchmark source")
    if limit < 1 or limit > 150 or offset < 0 or offset > 1000000:
        raise ValueError("Invalid pagination")
    data = entries[source]
    logics = sorted({r["logic"] for r in data})
    matched = [r for r in data if (not logic or r["logic"] == logic)
               and (not q or q.casefold() in r["path"].casefold())
               and (not ready_only or r["ready"])]
    return {"source": source, "count": len(matched), "total": len(data),
            "downloadable": sum(1 for r in data if r["ready"]),
            "logics": logics, "entries": matched[offset:offset + limit],
            "offset": offset, "limit": limit}


def fetch_benchmark(identifier: str) -> dict:
    _, lookup = catalog()
    if identifier not in lookup:
        raise ValueError("Benchmark is not in the trusted index")
    entry, mirror = lookup[identifier]
    if not mirror:
        raise LookupError(
            "This 2020-selected benchmark has no verified individual-file source. "
            "Historical 2020 files are hosted on StarExec; the catalog is available "
            "for browsing, but this entry cannot yet be fetched individually."
        )
    CACHE.mkdir(parents=True, exist_ok=True)
    path = CACHE / (hashlib.sha256(mirror.encode()).hexdigest() + ".smt2")
    cached = path.exists()
    if cached:
        data = path.read_bytes()
    else:
        # No URL entered by the browser is ever used. The trusted mirror
        # index is fixed in-repo and all user input is an opaque ID.
        url = RAW_BASE + mirror
        result = subprocess.run(
            ["curl", "--fail", "--silent", "--show-error", "--location",
             "--max-redirs", "2", "--proto", "=https", "--proto-redir", "=https",
             "--max-time", "25", "--max-filesize", str(MAX_FILE), url],
            capture_output=True, check=False)
        if result.returncode != 0:
            raise LookupError(
                "Download unavailable (" + result.stderr.decode("utf8", "replace")[:240] + ")"
            )
        data = result.stdout
        if not data:
            raise ValueError("The remote benchmark is empty")
        if len(data) > MAX_FILE:
            raise ValueError("Selected benchmark exceeds the 2 MiB download limit")
        if b"(assert" not in data and b"(check-sat" not in data:
            raise ValueError("Received data is not a recognizable SMT-LIB input")
        path.write_bytes(data)
    if len(data) > MAX_FILE:
        raise ValueError("Cached benchmark exceeds the size policy")
    try:
        source = data.decode("utf8")
    except UnicodeDecodeError as exc:
        raise ValueError("The benchmark is not UTF-8") from exc
    return {"source": source, "metadata": entry, "bytes": len(data),
            "cached": cached, "sha256": hashlib.sha256(data).hexdigest(),
            "download_source": RAW_BASE + mirror,
            "warning": ("This file is from the SMT-LIB 2025 mirror. "
                        "A matching 2020 path does not establish byte-for-byte 2020 identity.")}


if __name__ == "__main__":
    d = list_catalog("2020", logic="QF_LIA", ready_only=True)
    print("2020 QF_LIA available", d["count"])
    print("2020 entries", len(catalog()[0]["2020"]))
    print("2025 mirror entries", len(catalog()[0]["mirror"]))
