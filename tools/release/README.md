# Mozkey release candidate workflows: Windows and macOS

**Independent of signing providers.** These workflows generate unsigned
Windows MSI and unsigned, unnotarized macOS PKG **release candidates**, with
audit evidence. Neither workflow signs, notarizes, installs, or publishes its
packages. A successful Actions run is **not** release approval. The **exact
candidate bytes** may be published only after the manual promotion gate below
is satisfied; unsigned/unnotarized status must be disclosed.

## Windows x64 + ARM64 (`windows-release-candidate.yml`)

This workflow leaves `.github/workflows/windows.yaml` unchanged. Universal MSI
remains a normal CI test target, **not** a public release candidate.

### CI architecture

- On PR: source-contract regression tests only, with no secrets or build outputs.
- On manual `workflow_dispatch` from `main` only: `release_version` MUST match
  the canonical `src/version.bzl` public version (currently `1.0.0`).
  Exact `github.sha` and repository origin are validated.
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

## macOS Universal (`macos_zenz_installable_candidate.yml`)

- Manually dispatch from `main` with `release_version` matching the canonical
  `src/version.bzl` public version, e.g. `1.0.0`. The build fails closed for
  any other branch, repository, SHA mismatch, or version mismatch. The legacy
  development-branch `push` trigger is intentionally disabled.
- The package build must retain `--config=oss_macos`,
  `--define=macos_zenz_runtime=1`, and `--define=MOZKEY_INSTALLABLE=1`.
  **Ordinary guarded OSS PKGs reject installation; never promote them.**
- Source Bazel output remains `src/bazel-bin/mac/Mozc.pkg`. The workflow copies
  the identical bytes to `Mozkey_vX.Y.Z_macos_universal.pkg`. This is a
  filename change **only**; it must not modify the installer payload, receipt
  identifier, `101.0.0` technical version for v1.0.0, or code signature.
- The arm64 job audits the **packaged** preinstall, package identifier/version,
  Universal runtime, model, and Zenz scorer. The native Intel job verifies
  the exact same PKG SHA-256 and executes its native packaged-runtime probe.
  **Both jobs must pass** before any candidate is considered for publication.
- The versioned PKG is uploaded as a **candidate-only** artifact, with
  `ARTIFACT-CONTRACT.txt`, `BUILD-CONTRACT.txt`, and preinstall/arm64 audits.
  The separate Intel evidence is also mandatory. The workflow does not install
  the PKG, sign/notarize it, create tags, or publish a GitHub Release.
- The `macos.yaml` and `macos_zenz_formal_package_dual_native.yml` outputs
  remain independent CI evidence, not substitutes for this installable candidate.

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
5. Require the manual promotion gate below for publication. An automated
   provider-independent publication gate and optional signing adapter may be
   added later. If signing is required, **never** publish unsigned artifacts
   on signature or verification failure.

## Manual v1.0.0 release promotion gate

Do not promote CI artifacts automatically. Before creating the `v1.0.0` tag or
publishing the Release, record an explicit release-approval decision and check:

1. `main` contains all intended fixes and is frozen for candidate builds.
   Dispatch **both** unsigned candidate workflows from the **same exact SHA**
   with `release_version=1.0.0`; record both run IDs and attempts.
2. Check successful Windows **x64 and ARM64** candidates and their matching
   dictionary provenance, source-contract JSON, MSI/package-identity checks,
   and Windows native installer lifecycle/TSF tests.
3. Check successful macOS **arm64 and Intel** jobs for the **same Universal PKG**;
   confirm `ARTIFACT-CONTRACT.txt`, preinstall audit, runtime audit and digest.
4. Download the **exact versioned MSI/PKG bytes** from their respective runs.
   Check each SHA-256 against the CI evidence and record them in a release
   manifest; rename is permitted only if hashes remain unchanged.
5. Re-run physical install, previous-version upgrade, uninstallation, and
   Japanese/Zenz/UI tests on the new Windows x64 and macOS Apple Silicon
   candidate bytes. Record which Windows ARM64 and Intel macOS checks were CI
   only, and do not claim physical GUI installation without evidence.
6. Review third-party license notices, model and runtime provenance, offline
   checks, documentation, version numbers, and the **unsigned/unnotarized**
   warning. Generate `SHA256SUMS.txt` for precisely the packages to publish.
7. Approve release publication, create `v1.0.0` at the **same source SHA**, and
   manually attach only the audited files:
   `Mozkey_v1.0.0_x64.msi`, `Mozkey_v1.0.0_arm64.msi`,
   `Mozkey_v1.0.0_macos_universal.pkg`, and `SHA256SUMS.txt`.
   Compare the uploaded assets' checksums with the approved release manifest.

**No release on failure:** missing build/audit, mismatched source SHA, altered
package bytes, a failed physical upgrade, or unresolved licensing/signature
status must stop the promotion. Signing-provider approval is separate: if a
future release requires signing, never publish an unsigned fallback on a
signature or verification failure.

## Development and review

The release-candidate workflow is independent of any signing provider.

- Run `python -B -m unittest discover -s tools/release/tests -v`.
- Check PowerShell syntax and review all workflow changes.
- Windows PRs run source-contract tests only; the macOS candidate workflow
  is manually dispatched only.
- After merging, manually dispatch both candidate workflows from `main`.
- Supply the exact public version defined by `src/version.bzl`.
- Require successful x64 and ARM64 builds for the same source revision.
- Review both MSI artifacts and their verification evidence.
- Treat CI artifacts as **candidates only** until the manual promotion
  gate above is fully satisfied.

Real Windows installation, upgrade, Japanese input, Zenz operation,
and uninstall testing remain separate release acceptance requirements.

Policy references: `CODE_SIGNING_POLICY.md` and
`docs/security/release_checklist.md`.
