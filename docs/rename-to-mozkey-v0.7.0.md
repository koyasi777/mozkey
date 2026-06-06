# v0.7.0 Mozkey rename policy

This document records the rename policy used for the v0.7.0 release.

## Goal

v0.7.0 renames the public-facing product branding from Mozc / my-product to Mozkey.

The goal is to make the user-facing identity consistent while keeping internal Windows integration stable and keeping future upstream Mozc merges manageable.

## Public-facing name

The public-facing product name is:

- Mozkey
- Mozkey（もずきー）

## Changed in v0.7.0

The following areas are intentionally renamed to Mozkey:

- README public branding
- Windows installer display name and messages
- Windows IME display name
- Windows text service display name
- Windows candidate / suggest / indicator window accessible names
- About dialog product name and copyright text
- Help / product information URL
- Credits page product name and project URL
- Windows product icons
- macOS and Unix product icons
- Windows version resource product/company metadata

## Intentionally kept as Mozc

The following internal names are intentionally kept as Mozc for v0.7.0:

- Install directory: `C:\Program Files (x86)\Mozc`
- Binary names:
  - `mozc_server.exe`
  - `mozc_tool.exe`
  - `mozc_tip32.dll`
  - `mozc_tip64.dll`
  - `mozc_zenz_scorer.exe`
- MSI component and directory identifiers where changing them would increase upgrade risk
- UpgradeCode
- TSF / COM registration identifiers
- Service and task names where changing them could affect upgrade or registration behavior
- Source-level namespace, build target, and internal Mozc identifiers

These names are implementation details. Keeping them stable reduces installer risk and keeps future upstream Mozc merges easier.

## Upgrade policy

v0.7.0 is intended to upgrade cleanly from the previous v0.6.x my-product/Mozc fork line.

The MSI UpgradeCode is intentionally preserved so that Windows Installer treats v0.7.0 as an upgrade of the existing fork package rather than a separate product line.

## Non-goals for v0.7.0

The following are not part of the v0.7.0 rename:

- Full source namespace rename
- Binary filename rename
- Install directory rename
- TSF GUID rename
- COM class ID rename
- Windows service rename
- Complete removal of Mozc wording from source comments, upstream references, test data, or implementation details

## Copyright and attribution

Mozkey is based on Mozc.

User-facing copyright text may mention koyasi777 while preserving Google LLC attribution for Mozc-derived portions, for example:

`Copyright 2026 koyasi777. Portions Copyright 2010-2026 Google LLC.`

Credits and license files must continue to preserve upstream attribution.

## Validation checklist

Before publishing v0.7.0, verify:

- MSI builds successfully
- MSI administrative extract succeeds
- `mozc_tool.exe`, `mozc_server.exe`, `mozc_tip32.dll`, and `mozc_tip64.dll` contain Mozkey version-resource strings
- credits page contains Mozkey and the fork repository URL
- clean uninstall and normal install succeed
- IME list shows Mozkey
- About dialog shows Mozkey
- About dialog uses the Mozkey copyright text
- Settings dialog opens correctly
- Privacy tab layout is correct
- icons are updated
- normal conversion works
- live conversion works
- Zenz live correction works
- Google Japanese Input remains unaffected if installed side by side
