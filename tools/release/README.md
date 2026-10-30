# Mozkey Windows x64 + ARM64 unsigned release candidates (rehearsal)

**Independent of any signing provider.** Generates and verifies unsigned,
**NOT-FOR-RELEASE** x64 and native ARM64 MSI candidates and review evidence.
It intentionally does not sign, publish GitHub Releases, or change the existing
`.github/workflows/windows.yaml` jobs. Universal MSI remains in the existing
CI as a test target only; it is **not** a release candidate or publication target.

## CI architecture

- On PR: source-contract regression tests only, with no secrets or build outputs.
- On manual `workflow_dispatch` from `main` only: `release_version` MUST match
  the canonical `src/version.bzl` public version. Current baseline is 0.7.7,
  **not** 1.0.0. Exact `github.sha` and repository origin are validated.
- Dictionary source preparation is run **once** on `windows-2025` (the same
  runner type as the ordinary Windows CI); it uploads same-run generated data
  plus an independent SHA-256 inventory. No ARM64 WSL installation is assumed.
- Independent native package builds run on `windows-2025` for x64 and
  `windows-11-arm` for ARM64. Both download the **same-run** dictionary input
  artifact and fail if files differ from the recorded inventory. Build failure
  in one architecture does not cancel the other, but **both must pass** before
  the project can consider a future release candidate ready.
- No `actions/cache` is used. `update_deps.py` verifies pinned external
  archives. Qt uses `--target_arch=arm64` on ARM64, matching existing CI.
- Native ARM64 Bazel command mirrors `.github/workflows/windows.yaml`:
  `bazelisk build package --config release_build` on `windows-11-arm`.
  x64 retains `--config oss_windows --config release_build package`.
- The shared MSI verifier reads the Windows Installer database, checks identity
  and product version, requires unsigned status, administratively extracts
  (does not install) the MSI, and checks the architecture-specific hash and
  PE architecture of packaged llama-server and scorer, and the model SHA-256.
- Verified artifacts are named `Mozkey_vX.Y.Z_x64.msi` and
  `Mozkey_vX.Y.Z_arm64.msi`. Source build output remains `Mozc64.msi`;
  ProductCode/UpgradeCode and existing installer identities are not changed.
- Evidence is stored per architecture: source contract, dictionary input
  equality verification, MSI product identity, payload hashes, source revision,
  and candidate file SHA-256. The dictionary preparation inventory is a
  separate evidence artifact.
- Missing MSI or evidence is a hard error. No signing credentials, no
  `contents: write` and no release-publication step exist in this workflow.

## What the evidence does NOT prove

- Matching SHA-256 between dictionary-producing and packaging jobs proves
  byte integrity across these jobs; **it does not prove upstream dictionary
  provenance, content licensing, deterministic generation, or hermeticity**.
- Native compilation and administrative extraction do not establish real-world
  Japanese IME quality, successful updates, or complete security assurance.
- Some third-party Windows runtime binaries are checked into the public repo
  and are packaged rather than rebuilt from source here. Signing-provider
  origin-verification eligibility remains subject to independent review.
- CRT staging uses the available Visual Studio installation on each ephemeral
  runner. A recorded hash match confirms copies, not that the CRT version is
  cryptographically pinned across runner image updates.
- Tests and builds still need to execute on actual GitHub Windows runners.
  A successful local unit test is not an end-to-end release qualification.

## Required follow-up before publishing

1. Successful Actions run for **both** native architectures at the SAME source
   revision and dictionary inputs; inspect both MSI evidence manifests.
2. Existing `windows.yaml` ARM64 native install, TSF application-host,
   lifecycle, model hash, and x64/ARM64 comparison jobs must remain passing.
   These jobs are not bypassed or implicitly claimed as passing by this PR.
3. Review downloaded dictionary input revisions, license notices, CRT/toolchain
   provenance, source dependencies, and planned upgrade compatibility.
4. In Windows test VMs, run new installs, realistic prior-version upgrades,
   Zenz ON/OFF, real Japanese typing, uninstall and TSF registration checks.
5. After explicit approval, add a provider-independent publication gate and
   optional provider-specific signing adapter; once signing is required,
   **never** publish unsigned artifacts on signature or verification failure.

## Development and review

The release-candidate workflow is independent of any signing provider.

- Run `python -B -m unittest discover -s tools/release/tests -v`.
- Check PowerShell syntax and review all workflow changes.
- Pull requests run source-contract tests only.
- After merging, manually dispatch the workflow from `main`.
- Supply the exact public version defined by `src/version.bzl`.
- Require successful x64 and ARM64 builds for the same source revision.
- Review both MSI artifacts and their verification evidence.
- Do not publish rehearsal artifacts as official releases.

Real Windows installation, upgrade, Japanese input, Zenz operation,
and uninstall testing remain separate release acceptance requirements.

Policy references: `CODE_SIGNING_POLICY.md` and
`docs/security/release_checklist.md`.
