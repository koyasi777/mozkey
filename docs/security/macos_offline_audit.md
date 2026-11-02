# macOS offline contract (Mozkey)

This policy separates **offline behavior**, **regression detection**, and
**OS-enforced network prohibition**. Passing this gate must never be described
as a proof that a malicious or compromised process cannot access the Internet.

## Intended runtime data paths

* Mozc dictionary, settings and learning remain local.
* Mozc converter -> Zenz scorer: private per-user Unix-domain socket.
* Zenz scorer -> packaged `llama-server`: `127.0.0.1` only, random port,
  random API key; model is embedded in the PKG.
* Zenz scorer, guard and worker must not establish nonloopback IPv4/IPv6 sockets.
* Statistics reporting is disabled using the null stats implementation.
* macOS Secure Event Input prevents acquisition of surrounding text. Application
  input fields that do not enable Secure Event Input remain a separate risk.

## Gates

1. `python3 tools/check_macos_offline_source.py`: scans selected macOS and
   shared-runtime C/C++/Objective-C sources for network client APIs and
   unexpected IP socket use; checks critical loopback/secure-input contracts.
   The Windows-only `src/zenz_scorer/main.cc` is excluded **only** while
   Bazel explicitly maps `main_posix.cc` to macOS and `main.cc` to Windows;
   changing that platform mapping fails the source gate.
   Changes to the allowlisted networking source files require manual review.
   This is intentionally not a full transitive dependency audit.
2. `python3 -m unittest discover -s tools/tests -p 'test_macos_offline_audit.py' -v`:
   tests the gate and socket parser on representative safe and unsafe cases.
3. `src/mac/installer/zenz_runtime/verify_release_package.zsh`:
   audits **the unpacked exact PKG** on native Apple Silicon and Intel.
   It wraps the existing packaged scorer E2E probe with a sampled `ps` / `lsof`
   audit. The gate fails if it observes an IP socket whose local or peer address
   is not strictly `127.0.0.1` or `::1`, cannot observe expected Zenz processes,
   cannot query sockets, or the real inference probe fails. The JSON report is
   saved beside the package audit. It contains only PID, executable names and
   IP endpoint information, **not** prompts, user text or model outputs.
4. `.github/workflows/macos_secure_offline.yml` runs 1 and 2 on PR/push.
   Existing `macos_zenz_formal_package_dual_native.yml` and
   `macos_zenz_installable_candidate.yml` execute 3 through the package
   verifier; they do **not** install the candidate as a real IME.

## Limitations and remaining release work

* **Sampling gap:** `lsof` is observational. A short-lived connection between
  samples may be missed; this is not an outgoing network firewall. To claim a
  strict no-egress property, add OS-enforced process restrictions or a validated
  continuous network event trace.
* **Binary coverage gap:** a successful `codesign`, `otool -L`, or static symbol
  scan does not prove the absence of socket syscalls or dynamically loaded
  networking code. The source guard does not scan vendored `llama.cpp`, Qt, or
  the full transitive Mozc build graph.
* **Process coverage:** the package E2E focuses on packaged Zenz scorer/guard/
  worker. Before a release, perform an installed `Mozc.app` session test covering
  server, renderer, prelauncher, settings, dictionary tool, upgrade/uninstall,
  Zenz ON/OFF and macOS Secure Event Input; observe all associated processes.
* **Runtime enforcement:** consider App Sandbox network-client entitlements
  and/or an independently tested macOS-specific egress restriction only after
  confirming compatibility with InputMethodKit, local sockets and package
  signing. Do **not** ship an untested `sandbox-exec` / PF policy: macOS PF is not
  a straightforward per-app network firewall.
* **Release provenance:** verify the public release asset itself matches the
  audited commit and PKG checksum. CI-generated candidate packages may be
  unsigned/unnotarized and this audit is not a signing substitute.

MacOS CI builds can download *build dependencies* and model sources. That is
separate from network activity by a **running installed IME**.
