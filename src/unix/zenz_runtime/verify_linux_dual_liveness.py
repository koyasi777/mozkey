#!/usr/bin/env python3
"""Offline Linux dual-liveness contract smoke for pinned llama.cpp v0.7.0.

This compiles ONLY tools/server/main.cpp + a local fake llama_server stub.
It does not build llama.cpp, read a model, change IME settings, or access a network.
"""

import fcntl
import hashlib
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

PINNED_COMMIT = "620d2225cc436ab0b001e3200e590e3b286ef9ab"
PINNED_BLOB = "7f17c56a8c2989ba4071a50aa7828cfdcb50a08b"
MAC_PATCH_SHA256 = "3267e978f61456c32dfb83b15bf0f3a75393c5f98c946ee7a2846f7121b36dda"
PINNED_MAIN = (
    "int llama_server(int argc, char ** argv);\n\n"
    "int main(int argc, char ** argv) {\n"
    "    return llama_server(argc, argv);\n}\n"
)
STUB = r'''#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <unistd.h>

int llama_server(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--probe=plain") == 0) return 17;
  if (argc != 4 || std::strcmp(argv[1], "--probe=wait") != 0 ||
      std::strcmp(argv[2], "--api-key-file") != 0 ||
      std::strcmp(argv[3], "/proc/self/fd/25") != 0) return 81;
  for (const int fd : {23, 24}) {
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags < 0 || (flags & FD_CLOEXEC) == 0) return 82;
  }
  std::ifstream key(argv[3]);
  std::string line;
  if (!std::getline(key, line) || line != std::string(64, 'a')) return 83;
  while (true) ::pause();
}
'''


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run(*args: str, cwd: Path | None = None) -> None:
    subprocess.run(args, cwd=cwd, check=True, stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE, text=True)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def create_key_fd() -> int:
    fd = os.memfd_create("mozkey-dual-liveness-mock-key",
                         flags=os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
    try:
        os.write(fd, b"a" * 64 + b"\n")
        os.lseek(fd, 0, os.SEEK_SET)
        seal = (fcntl.F_SEAL_WRITE | fcntl.F_SEAL_SHRINK |
                fcntl.F_SEAL_GROW | fcntl.F_SEAL_SEAL)
        fcntl.fcntl(fd, fcntl.F_ADD_SEALS, seal)
        return fd
    except BaseException:
        os.close(fd)
        raise


def raise_fd(fd: int) -> int:
    high = fcntl.fcntl(fd, fcntl.F_DUPFD_CLOEXEC, 64)
    os.close(fd)
    return high


def wait_child(pid: int, timeout: float) -> int | None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        seen, status = os.waitpid(pid, os.WNOHANG)
        if seen == pid:
            return os.waitstatus_to_exitcode(status)
        time.sleep(0.02)
    return None


def spawn_with_lifetimes(executable: Path, close_channel: str,
                          malformed: bool = False) -> None:
    """Use posix_spawn file_actions as the production guard does."""
    read1, write1 = os.pipe2(os.O_CLOEXEC)
    read2, write2 = os.pipe2(os.O_CLOEXEC)
    key_fd = create_key_fd()
    pid: int | None = None
    try:
        if malformed:
            # Deliberately use a regular file in place of scorer's lifetime pipe.
            os.close(read1)
            read1 = -1
            with tempfile.TemporaryFile() as regular:
                read1 = raise_fd(os.dup(regular.fileno()))
        else:
            read1 = raise_fd(read1)
        read2 = raise_fd(read2)
        key_fd = raise_fd(key_fd)
        actions = [
            (os.POSIX_SPAWN_DUP2, read1, 23),
            (os.POSIX_SPAWN_DUP2, read2, 24),
            (os.POSIX_SPAWN_DUP2, key_fd, 25),
            (os.POSIX_SPAWN_CLOSE, read1),
            (os.POSIX_SPAWN_CLOSE, read2),
            (os.POSIX_SPAWN_CLOSE, key_fd),
        ]
        args = [str(executable), "--mozkey-zenz-dual-liveness-v1",
                "--probe=wait", "--api-key-file", "/proc/self/fd/25"]
        # Do not forward any of the caller's environment variables to the mock.
        pid = os.posix_spawn(str(executable), args,
                             {"PATH": "/usr/bin:/bin", "LANG": "C"},
                             file_actions=actions)
        if malformed:
            code = wait_child(pid, 2.5)
            require(code == 73, f"wrong FD kind must fail before llama_server: {code}")
            pid = None
            return
        # A live worker must not exit while both owners still hold their pipes.
        code = wait_child(pid, 0.2)
        require(code is None, f"worker exited early: {code}")
        if close_channel == "scorer":
            os.close(write1)
            write1 = -1
        else:
            os.close(write2)
            write2 = -1
        code = wait_child(pid, 3.0)
        require(code == 73, f"lifetime EOF={close_channel} must exit 73: {code}")
        pid = None
    finally:
        if pid is not None:
            # Kill only the exact child created by this test; never by process name.
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(pid, 0)
            except ChildProcessError:
                pass
        for fd in (read1, read2, key_fd, write1, write2):
            if fd >= 0:
                try:
                    os.close(fd)
                except OSError:
                    pass


def main() -> None:
    require(sys.platform.startswith("linux"), "Linux host required")
    source_root = Path(__file__).resolve().parents[2]
    mac_patch = (source_root / "mac/installer/zenz_runtime/patches/"
                 "llama-server-mozkey-dual-liveness.patch")
    linux_patch = (Path(__file__).resolve().parent / "patches/"
                   "llama-server-mozkey-linux-dual-liveness.patch")
    require(sha256(mac_patch) == MAC_PATCH_SHA256, "macOS patch identity mismatch")
    require(linux_patch.is_file(), "Linux overlay patch missing")
    fixture = PINNED_MAIN.encode("utf-8")
    blob = b"blob " + str(len(fixture)).encode() + b"\0" + fixture
    require(hashlib.sha1(blob).hexdigest() == PINNED_BLOB,
            "pinned upstream tools/server/main.cpp identity mismatch")

    with tempfile.TemporaryDirectory(prefix="mozkey_zenz_dual_liveness_") as temp:
        root = Path(temp)
        original = root / "tools/server/main.cpp"
        original.parent.mkdir(parents=True)
        original.write_bytes(fixture)
        run("git", "apply", "--check", str(mac_patch), cwd=root)
        run("git", "apply", str(mac_patch), cwd=root)
        run("git", "apply", "--unidiff-zero", "--check", str(linux_patch), cwd=root)
        run("git", "apply", "--unidiff-zero", str(linux_patch), cwd=root)
        require("#if defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))"
                in original.read_text(), "Linux branch absent")
        print("PATCH_CHAIN=PASS_PINNED_LLAMA_MAIN_MAC_PLUS_LINUX_OVERLAY", flush=True)
        stub = root / "stub.cc"
        stub.write_text(STUB)
        worker = root / "mock_llama_server"
        compiler = os.environ.get("CXX", "c++")
        run(compiler, "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror",
            str(original), str(stub), "-o", str(worker))
        print("LINUX_COMPILE=PASS_PINNED_MAIN_WITH_STUB", flush=True)

        no_switch = subprocess.run([str(worker), "--probe=plain"], timeout=2,
                                    check=False)
        require(no_switch.returncode == 17,
                f"unmodified launch must bypass extension: {no_switch.returncode}")
        print("NO_SWITCH_BEHAVIOR=PASS", flush=True)
        invalid = subprocess.run([str(worker), "--mozkey-zenz-dual-liveness-v1",
                                  "--probe=wait", "--api-key-file", "/proc/self/fd/25"],
                                 timeout=2, check=False)
        require(invalid.returncode == 73,
                f"invalid/missing lifetime FDs must fail: {invalid.returncode}")
        print("MISSING_FDS_FAIL_CLOSED=PASS", flush=True)
        spawn_with_lifetimes(worker, "scorer", malformed=True)
        print("REGULAR_FILE_FD_REJECTED=PASS", flush=True)
        spawn_with_lifetimes(worker, "scorer")
        print("SCORER_EOF_TERMINATES_WORKER=PASS", flush=True)
        spawn_with_lifetimes(worker, "guard")
        print("GUARD_EOF_TERMINATES_WORKER=PASS", flush=True)
    print("RESULT=PASS_Z1B_STAGE5_LINUX_DUAL_LIVENESS_OFFLINE_MOCK", flush=True)


if __name__ == "__main__":
    main()
