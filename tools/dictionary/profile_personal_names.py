#!/usr/bin/env python3
from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import sys
import unicodedata
from collections import Counter, defaultdict


def configure_stdio() -> None:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")


configure_stdio()


@dataclasses.dataclass(frozen=True)
class SystemEntry:
    key: str
    lid: int
    rid: int
    cost: int
    value: str


@dataclasses.dataclass(frozen=True)
class CostDecision:
    cost: int
    modifiers: tuple[str, ...]


@dataclasses.dataclass(frozen=True)
class PersonalNamePosIds:
    general: int
    given_name: int
    family_name: int


URL_RE = re.compile(r"^(https?://|www\.)", re.IGNORECASE)

# ゃゅょ は人名読みで普通に使うので reject しない。
# 例: きょうこ, しょう, じゅん, りょうこ
# 初期導入で落とすのは、表記遊び・ハンドル名に寄りやすい小書き母音だけ。
SMALL_VOWEL_KANA = set("ぁぃぅぇぉゎ")

GROUP_LIKE_VALUE_FRAGMENTS = (
    "ガールズ",
    "ボーイズ",
    "ユニット",
    "プロジェクト",
    "Project",
    "PROJECT",
    "チーム",
    "グループ",
    "劇団",
    "楽団",
    "隊",
)

PERSONAL_NAME_BASE_COST = 16000

# Full-name-like entries need to be stronger than generic weak personal-name
# entries. Otherwise the converter tends to split them into surname + given name
# and the full-name candidate may not appear in the candidate list.
PERSONAL_NAME_CORE_FULL_NAME_COST = 11500
PERSONAL_NAME_MIXED_FULL_NAME_COST = 12500

LONG_KATAKANA_RUN_THRESHOLD = 4

DANGEROUS_READING_SUFFIXES = (
    "したい",
    "について",
    "において",
    "によって",
    "として",
    "ように",
    "ために",
    "られる",
    "ました",
    "ません",
    "るな",
    "たい",
    "ない",
    "ます",
    "した",
    "して",
    "してる",
    "してた",
    "てる",
    "でる",
    "れる",
    "には",
    "にも",
    "では",
    "とは",
    "から",
    "まで",
    "より",
    "のに",
)

WATCH_KEYS = {
    "あい",
    "ゆい",
    "りん",
    "れん",
    "こう",
    "しょう",
    "けい",
    "まい",
    "はる",
    "なつ",
    "そら",
    "まこと",
    "ひかり",
    "みらい",
    "こころ",
    "きょうこ",
    "じゅん",
    "りょうこ",
}


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def normalize_text(s: str) -> str:
    return unicodedata.normalize("NFKC", s.strip())


def has_control_char(s: str) -> bool:
    return any(unicodedata.category(ch).startswith("C") for ch in s)


def has_symbol_or_punctuation(s: str) -> bool:
    for ch in s:
        cat = unicodedata.category(ch)
        if cat.startswith("P") or cat.startswith("S"):
            return True
    return False


def has_ascii_or_digit(s: str) -> bool:
    return any(ord(ch) < 128 for ch in s)


def is_hiragana_text(s: str) -> bool:
    return bool(s) and all("HIRAGANA" in unicodedata.name(ch, "") for ch in s)


def is_katakana_text(s: str) -> bool:
    return bool(s) and all("KATAKANA" in unicodedata.name(ch, "") for ch in s)


def has_small_vowel_kana(s: str) -> bool:
    return any(ch in SMALL_VOWEL_KANA for ch in s)


def is_cjk_ideograph(ch: str) -> bool:
    return "CJK UNIFIED IDEOGRAPH" in unicodedata.name(ch, "")


def has_cjk_ideograph(s: str) -> bool:
    return any(is_cjk_ideograph(ch) for ch in s)


def cjk_count(s: str) -> int:
    return sum(1 for ch in s if is_cjk_ideograph(ch))


def has_hiragana_or_katakana(s: str) -> bool:
    for ch in s:
        name = unicodedata.name(ch, "")
        if "HIRAGANA" in name or "KATAKANA" in name:
            return True
    return False


def is_core_full_name_like(entry: SystemEntry) -> bool:
    value_len = len(entry.value)
    cjk_len = cjk_count(entry.value)

    return (
        len(entry.key) >= 5
        and 3 <= value_len <= 5
        and cjk_len == value_len
    )


def is_mixed_full_name_like(entry: SystemEntry) -> bool:
    value_len = len(entry.value)
    cjk_len = cjk_count(entry.value)

    return (
        len(entry.key) >= 5
        and 3 <= value_len <= 5
        and cjk_len >= 2
        and has_hiragana_or_katakana(entry.value)
        and is_cjk_ideograph(entry.value[0])
        and max_katakana_run_length(entry.value) <= 2
    )


def pos_ids_for_entry(
    entry: SystemEntry,
    pos_ids: PersonalNamePosIds,
) -> tuple[int, int]:
    if is_core_full_name_like(entry) or is_mixed_full_name_like(entry):
        return pos_ids.family_name, pos_ids.given_name

    return pos_ids.general, pos_ids.general


def kana_count(s: str) -> int:
    n = 0
    for ch in s:
        name = unicodedata.name(ch, "")
        if "HIRAGANA" in name or "KATAKANA" in name:
            n += 1
    return n


def max_katakana_run_length(s: str) -> int:
    current = 0
    maximum = 0

    for ch in s:
        name = unicodedata.name(ch, "")
        if "KATAKANA" in name:
            current += 1
            maximum = max(maximum, current)
        else:
            current = 0

    return maximum


def has_group_like_value_fragment(s: str) -> bool:
    return any(fragment in s for fragment in GROUP_LIKE_VALUE_FRAGMENTS)


def has_dangerous_reading_suffix(key: str) -> bool:
    for suffix in DANGEROUS_READING_SUFFIXES:
        if key.endswith(suffix) and len(key) >= len(suffix) + 2:
            return True
    return False


def parse_system_line(line: str) -> SystemEntry | None:
    cols = line.rstrip("\n\r").split("\t")
    if len(cols) != 5:
        return None

    key, lid, rid, cost, value = cols

    try:
        return SystemEntry(
            key=normalize_text(key),
            lid=int(lid),
            rid=int(rid),
            cost=int(cost),
            value=normalize_text(value),
        )
    except ValueError:
        return None


def find_pos_id(id_def: pathlib.Path, pos_name: str) -> tuple[int, str]:
    lines = id_def.read_text(encoding="utf-8", errors="replace").splitlines()

    for line in lines:
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue

        parts = stripped.split(maxsplit=1)
        if len(parts) != 2:
            continue

        try:
            pos_id = int(parts[0])
        except ValueError:
            continue

        name = parts[1]
        if name == pos_name:
            return pos_id, stripped

    raise RuntimeError(f"failed to find POS id in {id_def}: {pos_name}")


def find_personal_name_pos_ids(id_def: pathlib.Path) -> PersonalNamePosIds:
    general_id, general_matched = find_pos_id(
        id_def,
        "名詞,固有名詞,人名,一般,*,*,*",
    )
    given_id, given_matched = find_pos_id(
        id_def,
        "名詞,固有名詞,人名,名,*,*,*",
    )
    family_id, family_matched = find_pos_id(
        id_def,
        "名詞,固有名詞,人名,姓,*,*,*",
    )

    print("POS:")
    print(f"  general:     {general_matched}")
    print(f"  family_name: {family_matched}")
    print(f"  given_name:  {given_matched}")
    print("")

    return PersonalNamePosIds(
        general=general_id,
        given_name=given_id,
        family_name=family_id,
    )


def load_existing_signatures(paths: list[pathlib.Path]) -> set[tuple[str, str]]:
    signatures: set[tuple[str, str]] = set()

    for path in paths:
        if not path.exists():
            print(f"warning: existing dictionary not found: {path}", file=sys.stderr)
            continue

        with path.open("r", encoding="utf-8-sig", errors="replace", newline="") as f:
            for line in f:
                entry = parse_system_line(line)
                if entry is None:
                    continue
                signatures.add((entry.key, entry.value))

    return signatures


def load_base_dictionary_paths(root: pathlib.Path) -> list[pathlib.Path]:
    dictionary_oss = root / "src" / "data" / "dictionary_oss"
    return sorted(dictionary_oss.glob("dictionary[0-9][0-9].txt"))


def reject_reason(entry: SystemEntry) -> str | None:
    if "\ufffd" in entry.key or "\ufffd" in entry.value:
        return "replacement_char"

    if has_control_char(entry.key) or has_control_char(entry.value):
        return "control_char"

    if URL_RE.search(entry.value):
        return "url"

    if has_symbol_or_punctuation(entry.value):
        return "symbol_or_punctuation_value"

    if has_ascii_or_digit(entry.value):
        return "ascii_or_digit_value"

    # ゃゅょ は reject しない。ぁぃぅぇぉゎ だけを表記遊び寄りとして落とす。
    if has_small_vowel_kana(entry.key):
        return "small_vowel_kana_reading"

    if len(entry.value) <= 1:
        return "short_value_len_1"

    if entry.key == entry.value and is_hiragana_text(entry.value):
        return "same_hiragana_key_value"

    # 人名辞書としては、明らかなユニット名・グループ名は初期導入で外す。
    if has_group_like_value_fragment(entry.value):
        return "group_like_value"

    return None


def cost_for(entry: SystemEntry) -> CostDecision:
    modifiers: list[str] = []

    if is_core_full_name_like(entry):
        cost = max(entry.cost, PERSONAL_NAME_CORE_FULL_NAME_COST)
        modifiers.append("core_full_name_like")
    elif is_mixed_full_name_like(entry):
        cost = max(entry.cost, PERSONAL_NAME_MIXED_FULL_NAME_COST)
        modifiers.append("mixed_full_name_like")
    else:
        cost = max(entry.cost, PERSONAL_NAME_BASE_COST)

    key_len = len(entry.key)
    value_len = len(entry.value)

    if key_len <= 1:
        cost += 7000
        modifiers.append("short_reading_len_1")
    elif key_len == 2:
        cost += 6000
        modifiers.append("short_reading_len_2")
    elif key_len == 3:
        cost += 4500
        modifiers.append("short_reading_len_3")
    elif key_len == 4:
        cost += 2500
        modifiers.append("short_reading_len_4")

    if value_len == 2:
        cost += 2500
        modifiers.append("short_value_len_2")

    if is_katakana_text(entry.value):
        cost += 1500
        modifiers.append("katakana_value")

    if is_hiragana_text(entry.value):
        cost += 3000
        modifiers.append("hiragana_value")

    # 長い芸名・ハンドル名・ユニット名風の候補を弱める。
    if value_len >= 12:
        cost += 5000
        modifiers.append("very_long_value")
    elif value_len >= 8:
        cost += 2500
        modifiers.append("long_value")

    # 長いカタカナ塊を含む値は、芸名・ユニット名・ハンドル名に寄りやすい。
    # 例:
    #   愛嬌レスポンス
    #   タイガー桜井
    #   藍色アステリズム
    if max_katakana_run_length(entry.value) >= LONG_KATAKANA_RUN_THRESHOLD:
        cost += 2500
        modifiers.append("long_katakana_run")

    # 漢字を含まない長い kana-only 名は、人名というより芸名・ハンドル名・団体名に寄りやすい。
    if not has_cjk_ideograph(entry.value) and kana_count(entry.value) >= 6:
        cost += 2500
        modifiers.append("long_kana_only_value")

    if has_dangerous_reading_suffix(entry.key):
        cost += 5000
        modifiers.append("dangerous_reading_suffix")

    cost = max(0, min(cost, 20000))

    return CostDecision(cost=cost, modifiers=tuple(modifiers))


def convert(
    input_path: pathlib.Path,
    output_path: pathlib.Path,
    existing_paths: list[pathlib.Path],
    pos_ids: PersonalNamePosIds,
) -> int:
    existing = load_existing_signatures(existing_paths)

    stats = Counter()
    rejected_examples: dict[str, list[SystemEntry]] = defaultdict(list)
    seen_new: set[tuple[str, str]] = set()
    output: list[SystemEntry] = []

    watch_total = Counter()
    watch_strong = Counter()
    watch_examples: dict[str, list[SystemEntry]] = {key: [] for key in WATCH_KEYS}
    strong_cost_threshold = 15000

    lowest_examples: list[SystemEntry] = []

    with input_path.open("r", encoding="utf-8-sig", errors="replace", newline="") as f:
        for line in f:
            stats["input"] += 1

            entry = parse_system_line(line)
            if entry is None:
                stats["skipped_non_entry"] += 1
                continue

            reason = reject_reason(entry)
            if reason is not None:
                stats[f"rejected_{reason}"] += 1
                if len(rejected_examples[reason]) < 8:
                    rejected_examples[reason].append(entry)
                continue

            signature = (entry.key, entry.value)

            if signature in existing:
                stats["duplicate_existing"] += 1
                continue

            if signature in seen_new:
                stats["duplicate_new"] += 1
                continue

            seen_new.add(signature)

            decision = cost_for(entry)
            for modifier in decision.modifiers:
                stats[f"modifier_{modifier}"] += 1

            lid, rid = pos_ids_for_entry(entry, pos_ids)

            profiled = SystemEntry(
                key=entry.key,
                lid=lid,
                rid=rid,
                cost=decision.cost,
                value=entry.value,
            )
            output.append(profiled)

            if len(lowest_examples) < 200:
                lowest_examples.append(profiled)
            else:
                worst_idx = max(range(len(lowest_examples)), key=lambda i: lowest_examples[i].cost)
                if profiled.cost < lowest_examples[worst_idx].cost:
                    lowest_examples[worst_idx] = profiled

            if profiled.key in WATCH_KEYS:
                watch_total[profiled.key] += 1
                if profiled.cost <= strong_cost_threshold:
                    watch_strong[profiled.key] += 1
                    if len(watch_examples[profiled.key]) < 10:
                        watch_examples[profiled.key].append(profiled)

    output.sort(key=lambda e: (e.key, e.value, e.cost))
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with output_path.open("w", encoding="utf-8", newline="\n") as f:
        for entry in output:
            f.write(f"{entry.key}\t{entry.lid}\t{entry.rid}\t{entry.cost}\t{entry.value}\n")

    stats["output"] = len(output)

    print("Input:")
    print(f"  {input_path}")
    print("Output:")
    print(f"  {output_path}")
    print("")
    print("Stats:")
    for key in sorted(stats.keys()):
        print(f"  {key}: {stats[key]}")

    print("")
    print("Cost policy:")
    print(f"  base: max(original_cost, {PERSONAL_NAME_BASE_COST})")
    print(f"  core full-name-like:  max(original_cost, {PERSONAL_NAME_CORE_FULL_NAME_COST})")
    print(f"  mixed full-name-like: max(original_cost, {PERSONAL_NAME_MIXED_FULL_NAME_COST})")
    print("  reading len 1: +7000")
    print("  reading len 2: +6000")
    print("  reading len 3: +4500")
    print("  reading len 4: +2500")
    print("  value len 2:   +2500")
    print("  hiragana value:+3000")
    print("  katakana value:+1500")
    print("  value len >= 8:+2500")
    print("  value len >=12:+5000")
    print("  long katakana run >= 4:+2500")
    print("  long kana-only:+2500")
    print("  dangerous suffix: +5000")
    print("  clamp: 0..20000")

    print("")
    print("Rejected examples:")
    for reason in sorted(rejected_examples.keys()):
        print(f"  {reason}:")
        for entry in rejected_examples[reason]:
            print(f"    {entry.key}\t{entry.lid}\t{entry.rid}\t{entry.cost}\t{entry.value}")

    print("")
    print(f"Watch keys, strong cost <= {strong_cost_threshold}:")
    for key in sorted(WATCH_KEYS):
        print(f"  {key}: total={watch_total[key]}, strong={watch_strong[key]}")
        for entry in watch_examples[key]:
            print(f"    {entry.key}\t{entry.lid}\t{entry.rid}\t{entry.cost}\t{entry.value}")

    print("")
    print("Lowest cost examples:")
    for entry in sorted(lowest_examples, key=lambda e: (e.cost, e.key, e.value))[:80]:
        print(f"  {entry.key}\t{entry.lid}\t{entry.rid}\t{entry.cost}\t{entry.value}")

    return 0


def main() -> int:
    root = repo_root()

    parser = argparse.ArgumentParser(
        description="Profile mozcdic-ut-personal-names for Mozkey daily dictionary trial."
    )
    parser.add_argument(
        "--input",
        type=pathlib.Path,
        default=root / "src" / "data" / "dictionary_koyasi" / "generated" / "personal_names" / "mozcdic-ut-personal-names.txt",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=root / "src" / "data" / "dictionary_koyasi" / "generated" / "profiled" / "mozcdic-ut-personal-names-daily.txt",
    )
    parser.add_argument(
        "--daily",
        type=pathlib.Path,
        default=root / "src" / "data" / "dictionary_koyasi" / "generated" / "profiled" / "mozcdic-ut-daily.txt",
    )
    parser.add_argument(
        "--nico-pixiv-delta",
        type=pathlib.Path,
        default=root / "src" / "data" / "dictionary_koyasi" / "generated" / "profiled" / "dic-nico-pixiv-delta.txt",
    )
    parser.add_argument(
        "--id-def",
        type=pathlib.Path,
        default=root / "src" / "data" / "dictionary_oss" / "id.def",
    )
    parser.add_argument(
        "--no-base-dictionary",
        action="store_true",
        help="Do not compare against src/data/dictionary_oss/dictionary00.txt ... dictionary09.txt.",
    )

    args = parser.parse_args()

    if not args.input.exists():
        print(f"error: input does not exist: {args.input}", file=sys.stderr)
        return 1

    if not args.id_def.exists():
        print(f"error: id.def does not exist: {args.id_def}", file=sys.stderr)
        return 1

    existing_paths = [args.daily]

    if args.nico_pixiv_delta.exists():
        existing_paths.append(args.nico_pixiv_delta)
    else:
        print(
            f"warning: nico/pixiv delta not found: {args.nico_pixiv_delta}",
            file=sys.stderr,
        )

    if not args.no_base_dictionary:
        existing_paths.extend(load_base_dictionary_paths(root))

    pos_ids = find_personal_name_pos_ids(args.id_def)

    return convert(
        input_path=args.input,
        output_path=args.output,
        existing_paths=existing_paths,
        pos_ids=pos_ids,
    )


if __name__ == "__main__":
    raise SystemExit(main())
