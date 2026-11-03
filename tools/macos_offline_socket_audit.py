#!/usr/bin/env python3
"""Observe packaged macOS Zenz scorer and descendants during a real inference.

This is a fail-closed sampled socket audit, NOT an OS firewall and NOT proof
against a connection opened and closed between samples. Uses only macOS ps/lsof.
Report contains process IDs and endpoints, never prompts or model output.
"""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def parse_process_tree(ps_output: str, parent_pid: int) -> dict[int, str]:
    entries: dict[int, tuple[int, str]] = {}
    for line in ps_output.splitlines():
        fields = line.strip().split(None, 2)
        if len(fields) != 3:
            continue
        try:
            pid, ppid = int(fields[0]), int(fields[1])
        except ValueError:
            continue
        entries[pid] = (ppid, fields[2])
    descendants = {parent_pid}
    changed = True
    while changed:
        old_size = len(descendants)
        descendants.update(pid for pid, (ppid, _) in entries.items() if ppid in descendants)
        changed = len(descendants) != old_size
    return {pid: entries[pid][1] for pid in descendants if pid in entries}


def parse_lsof_names(stdout: str) -> list[tuple[int, str]]:
    pid: int | None = None
    names: list[tuple[int, str]] = []
    for line in stdout.splitlines():
        if line.startswith("p") and line[1:].isdigit():
            pid = int(line[1:])
        elif line.startswith("n") and pid is not None:
            names.append((pid, line[1:]))
    return names


def endpoint_host(endpoint: str) -> str:
    endpoint = endpoint.strip()
    if endpoint.startswith("["):
        closing = endpoint.find("]:")
        if closing <= 0:
            raise ValueError("malformed IPv6 endpoint")
        return endpoint[1:closing]
    if ":" not in endpoint:
        raise ValueError("endpoint has no port")
    return endpoint.rsplit(":", 1)[0]


def validate_loopback_name(name: str) -> str | None:
    """Return reason if any IP endpoint is non-loopback or unparseable."""
    for endpoint in name.split("->"):
        try:
            host = endpoint_host(endpoint)
            address = ipaddress.ip_address(host.split("%", 1)[0])
        except ValueError:
            return f"unparseable_ip_endpoint:{name}"
        # Strict address contract. ::ffff:127.0.0.1 is not whitelisted.
        if str(address) not in ("127.0.0.1", "::1"):
            return f"nonloopback_ip_endpoint:{name}"
    return None


def scan_sockets(scorer_pid: int) -> tuple[dict[int, str], list[tuple[int, str]], list[str]]:
    errors: list[str] = []
    proc = subprocess.run(
        ["/bin/ps", "-axo", "pid=,ppid=,comm="],
        capture_output=True, text=True, check=False,
    )
    if proc.returncode != 0:
        return {}, [], ["ps_failed:" + proc.stderr.strip()[:300]]
    tree = parse_process_tree(proc.stdout, scorer_pid)
    if scorer_pid not in tree:
        return {}, [], ["scorer_process_disappeared"]
    pids = ",".join(map(str, sorted(tree)))
    ls = subprocess.run(
        ["/usr/sbin/lsof", "-nP", "-a", "-p", pids, "-iTCP", "-iUDP", "-FpcnT"],
        capture_output=True, text=True, check=False,
    )
    if ls.returncode not in (0, 1) or (ls.returncode != 0 and ls.stderr.strip()):
        errors.append(f"lsof_failed:{ls.returncode}:{ls.stderr.strip()[:300]}")
    names = parse_lsof_names(ls.stdout)
    if any(pid not in tree for pid, _ in names):
        errors.append("unexpected_lsof_process")
    for pid, name in names:
        reason = validate_loopback_name(name)
        if reason is not None:
            errors.append(f"pid={pid}:{reason}")
    return tree, names, errors


def run_audit(scorer_pid: int, command: list[str], interval: float, report: Path) -> int:
    errors: list[str] = []
    seen_processes: set[str] = set()
    seen_sockets: set[str] = set()
    samples = 0
    saw_scorer = False
    saw_llama = False
    saw_llama_listener = False
    result: int | None = None

    if sys.platform != "darwin":
        print("MACOS_OFFLINE_SOCKET_AUDIT=NOT_MACOS", file=sys.stderr)
        return 2
    if not Path("/usr/sbin/lsof").is_file():
        print("MACOS_OFFLINE_SOCKET_AUDIT=NO_LSOF", file=sys.stderr)
        return 2
    if scorer_pid <= 0 or not command:
        return 2
    if not Path("/bin/ps").is_file():
        return 2

    child: subprocess.Popen | None = None
    try:
        child = subprocess.Popen(command)
        while True:
            tree, names, scan_errors = scan_sockets(scorer_pid)
            samples += 1
            errors.extend(scan_errors)
            for pid, executable in tree.items():
                basename = os.path.basename(executable)
                seen_processes.add(basename)
                if pid == scorer_pid and basename in ("mozc_zenz_scorer", "mozc_zenz_score"):
                    saw_scorer = True
                if basename == "llama-server":
                    saw_llama = True
                    if any(npid == pid and name.startswith("127.0.0.1:") for npid, name in names):
                        saw_llama_listener = True
            seen_sockets.update(name for _, name in names)
            # Preserve all violations; no false success when lsof/ps fails.
            if errors:
                child.terminate()
                break
            if child.poll() is not None:
                break
            time.sleep(interval)
        result = child.wait(timeout=10)
        if result != 0:
            errors.append(f"inference_probe_failed:{result}")
    except (OSError, subprocess.SubprocessError) as error:
        errors.append(f"audit_execution_failed:{type(error).__name__}:{error}")
        if child is not None and child.poll() is None:
            child.kill()
            child.wait()
    if not saw_scorer:
        errors.append("packaged_scorer_not_observed")
    if not saw_llama:
        errors.append("llama_server_not_observed")
    if not saw_llama_listener:
        errors.append("llama_loopback_listener_not_observed")
    if samples == 0:
        errors.append("no_samples")
    status = "FAIL" if errors else "PASS"
    record = {
        "status": status,
        "scorer_pid": scorer_pid,
        "samples": samples,
        "sample_interval_seconds": interval,
        "probe_exit_code": result,
        "observed_executables": sorted(seen_processes),
        "observed_ip_endpoints": sorted(seen_sockets),
        "errors": sorted(set(errors)),
        "coverage": "sampled open IPv4/IPv6 sockets of packaged scorer and observed descendants; NOT a firewall",
    }
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(record, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"MACOS_OFFLINE_SOCKET_AUDIT={status}")
    print(f"MACOS_OFFLINE_SOCKET_SAMPLES={samples}")
    print(f"MACOS_OFFLINE_SOCKET_REPORT={report}")
    for error in sorted(set(errors)):
        print(f"MACOS_OFFLINE_SOCKET_FAILURE={error}", file=sys.stderr)
    return 1 if errors else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scorer-pid", type=int, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--interval", type=float, default=0.15)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    opts = parser.parse_args()
    cmd = opts.command[1:] if opts.command and opts.command[0] == "--" else opts.command
    if not cmd or opts.interval < 0.05 or opts.interval > 1.0:
        parser.error("provide '-- <inference probe> ...' and interval in [0.05, 1.0]")
    return run_audit(opts.scorer_pid, cmd, opts.interval, opts.report)


if __name__ == "__main__":
    sys.exit(main())
