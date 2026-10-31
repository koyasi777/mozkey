# Mozkey bundled dictionary — source and attribution notices

This document accompanies Mozkey distributions that include the locally generated
`daily` dictionary. It describes upstream materials and transformations; it does
not replace the governing licenses or establish that every third-party right has
been independently cleared.

The dictionary is generated during the release build. The source revisions and
input hashes are recorded in `tools/dictionary/locks/daily_sources.json` at the
release source commit. Generated outputs are not tracked in Git by default.

## Mozc base dictionary

- Source: https://github.com/google/mozc
- Input: `src/data/dictionary_oss/` and `id.def` in the Mozkey fork.
- License/attribution: mixed source notices, including IPAdic, ICOT and Okinawa
  dictionary material, in `src/data/dictionary_oss/README.txt` and the bundled
  `credits_en.html`. Mozc source code is under BSD-3-Clause; this does **not**
  mean every dictionary entry is separately BSD-3-Clause.

## merge-ut-dictionaries — dictionary construction

- Source: https://github.com/utuhiro78/merge-ut-dictionaries
- Locked revision: `640844881690e877d0e54dff024edb8ec7b77290`
- The upstream README calls its generated dictionary `Mixed`, while its LICENSE
  file calls it `Combined`. These are upstream descriptions of mixed-source
  material, **not** a single SPDX license applicable to every entry.
- Source scripts: Apache License 2.0 (as reported by upstream).
- Mozkey uses a locally adapted merge process against Mozkey's own Mozc
  dictionary, removes duplicates, calculates word costs, and applies the
  `daily`-profile filters. This is a generated/modified dataset, not an
  unmodified redistribution of a source dictionary.

### Place names / Japan Post postal-code data

- Source: https://github.com/utuhiro78/mozcdic-ut-place-names
- Locked revision: `98cf8e9d7731695a52e5bc72ed633933c71d1913`
- Original data: Japan Post postal-code data,
  https://www.post.japanpost.jp/zipcode/download.html
- Upstream notice: Japan Post does not claim copyright over the postal-code
  data; see https://www.post.japanpost.jp/zipcode/dl/readme.html . Do not
  generalize that statement to unrelated Japan Post publications.

### SudachiDict-derived entries

- Converted dictionary: https://github.com/utuhiro78/mozcdic-ut-sudachidict
- Locked revision: `13129ddebe1cb8eabb63feccbe157adec4e1de01`
- Original source: https://github.com/WorksApplications/SudachiDict
- Upstream references Apache License 2.0 **and** the legal notices for UniDic
  and NEologd-derived resources. The accompanying `SudachiDict-LEGAL.txt`
  preserves these third-party attributions and disclaimers.
- Source of upstream legal notice:
  https://github.com/WorksApplications/SudachiDict/blob/develop/LEGAL
- Apache-2.0 license text is provided in the Zenz runtime license directory in
  the v1.0.0 Zenz-bundled Windows and macOS builds. The SudachiDict notice is
  **not** an assertion that the independent `mozcdic-ut-neologd` UT dictionary
  is enabled; it is an upstream dependency notice for Sudachi-derived data.

### Japanese Wikipedia index used for cost calculation

- Source: Japanese Wikipedia dump index, Wikimedia Foundation
- Snapshot: `20261001`
- Locked file:
  `jawiki-20261001-pages-articles-multistream-index.txt.bz2`
- Source URL:
  https://dumps.wikimedia.org/jawiki/20261001/jawiki-20261001-pages-articles-multistream-index.txt.bz2
- Locked SHA-256:
  `8A040564302DC0BA57160172ABEF3D4F69CC7CA36EA67B6439959D441CD037E7`
- Upstream merge-ut labels this index as CC BY-SA material and uses titles to
  calculate costs for existing entries. See the Japanese Wikipedia reuse
  explanation: https://ja.wikipedia.org/wiki/Wikipedia:%E3%82%A6%E3%82%A3%E3%82%AD%E3%83%9A%E3%83%87%E3%82%A3%E3%82%A2%E3%82%92%E4%BA%8C%E6%AC%A1%E5%88%A9%E7%94%A8%E3%81%99%E3%82%8B
  and https://creativecommons.org/licenses/by-sa/4.0/ .
- Attribution: Wikipedia contributors (Japanese Wikipedia), via Wikimedia
  projects. Mozkey uses a filtered index during dictionary generation; it does
  not enable the separate `mozcdic-ut-jawiki` vocabulary dictionary in `daily`.
- The upstream labels the index `CC BY-SA` without a version. This document
  does not assert that using a title-based cost signal necessarily creates a
  CC BY-SA adaptation of the entire generated Mozkey dictionary. Confirm the
  applicable source license version and any attribution/adaptation obligations
  against the source snapshot before release approval.

## Personal names

- Source: https://github.com/utuhiro78/mozcdic-ut-personal-names
- Locked revision: `b557dd4c1548cc7c2164d5f8b14170c40cbb6f81`
- Upstream license: Apache License 2.0.
- Mozkey filters and cost-adjusts these entries for its daily profile.

## Nico Nico Encyclopedia / pixiv Encyclopedia intersection

- Imported source: https://github.com/Niccori250k/dic-nico-intersection-pixiv
- Locked revision: `4d0d8d219d0b9abf34745016e398e762182fce61`
- Original project: https://github.com/ncaq/dic-nico-intersection-pixiv
- Source data: words common to Nico Nico Encyclopedia and pixiv Encyclopedia,
  with readings from Nico Nico Encyclopedia according to the upstream README.
- Upstream says its **code** is MIT-licensed and its maintainer does not claim
  copyright over the **generated dictionary**. That statement is **not** a
  license grant on behalf of the third-party encyclopedia/data owners.
- Mozkey performs conversion, filtering and cost adjustments.
- Third-party rights and source-data reuse terms require a separate release
  determination; do not treat the upstream maintainer's non-assertion as a
  blanket authorization.

## Mozkey syntax-guard dictionary

- Generated by `tools/dictionary/generate_syntax_guard_dictionary.py` within
  the Mozkey source tree; no additional external source declared for this
  generated component.

## Redistribution and other dependencies

This file should be supplied **with the binary package**, not merely linked
from a repository. For v1.0.0, the Windows MSI carries these two dictionary
notices under `documents/`, and the macOS PKG carries them under the app's
`Contents/Resources/` directory.

Additional Zenz model and runtime notices are distributed separately in each
platform's `ZenzRuntime/licenses/` (macOS) or `licenses/` (Windows). The existing
`credits_en.html` covers additional base software and dictionary material.

Before publication, separately verify the terms applying to the final
third-party dictionary data (especially scraped data and Wikipedia use) and
any Qt LGPLv3 distribution obligations. Providing a notice document alone does
not establish compliance with all applicable license conditions.
