# Mozkey bundled romaji table presets

This directory contains the four bundled romaji-table presets and their license notices. These TSV inputs are also embedded into `src/config/roman_table_presets.cc` using `src/config/generate_roman_table_presets_cc.py`.

| Preset | TSV | License notice |
| --- | --- | --- |
| Simplified-AZIK | `simplified_azik.tsv` | `simplified_azik.LICENSE` (MIT; tawara) |
| 新月配列 (Shingetsu) | `shingetsu.tsv` | `shingetsu.LICENSE` (MIT; nagamine-git) |
| せおと配列 (Seoto) | `seoto.tsv` | `seoto.LICENSE` (MIT; koyasi777), `seoto.BSD-LICENSE` (BSD-style third-party notice) |
| せおと配列 Plus (Seoto Plus) | `seoto_plus.tsv` | Same Seoto notices |

The Seoto Plus preset extends Seoto with consecutive-consonant rules and rules using `DisplayAmbiguousResult` to display output during ambiguous input. The preset rules and their original ordering are preserved; automatic alphabetical sorting would change Mozc romaji-table semantics.

## Runtime behavior

- Bundled presets are resolved through `config::GetRomanTablePreset`. If an unmodified bundled preset is selected, `NormalizeRomanTablePreset` can replace an unchanged saved snapshot with the newly bundled table on upgrade. A changed or externally modified table is treated as custom and is not overwritten.
- Personal presets are stored in the Mozkey configuration, subject to the editor's size and count limits. Personal preset changes are staged until the containing settings dialog is accepted.
- The table editor navigates search matches without sorting or filtering. The currently matched row is outlined and can be selected for editing/removal.
- Before saving, an incomplete rule is rejected. Import into the current editor uses a full parse before replacing visible rows. Conflicting duplicate input keys prompt for confirmation when saving a personal preset; identical rules do not count as a conflict.
- The editor exposes these notices through its built-in Origins and Licenses dialog.

## Reproducibility and review

Keep all TSV and license files together. When changing a bundled TSV, regenerate `src/config/roman_table_presets.cc` with `src/config/generate_roman_table_presets_cc.py`, review the generated diff, and rerun the Bazel tests `//config:roman_table_presets_test`, `//gui/config_dialog:roman_table_rules_test`, and `//composer:table_test`. The normal Windows GUI and packaging checks are also required before releasing.

Historical development or worktree-recovery instructions are intentionally not stored in this data directory. They belong in a separate development note and must not be mistaken for current build requirements.
