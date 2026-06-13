#!/usr/bin/env python3
from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import sys
import unicodedata
from collections import Counter
from typing import Iterable


@dataclasses.dataclass(frozen=True)
class Entry:
    key: str
    lid: int
    rid: int
    cost: int
    value: str


@dataclasses.dataclass
class Stats:
    input_lines: int = 0
    output_lines: int = 0
    rejected_bad_columns: int = 0
    rejected_empty: int = 0
    rejected_bad_number: int = 0
    rejected_control_char: int = 0
    rejected_replacement_char: int = 0
    rejected_symbol_only: int = 0
    rejected_url: int = 0
    duplicates: int = 0
    demoted_short_reading_1: int = 0
    demoted_short_reading_2: int = 0
    demoted_short_reading_3: int = 0
    demoted_short_reading_4: int = 0
    floored_short_reading_1: int = 0
    floored_short_reading_2: int = 0
    floored_short_reading_3: int = 0
    floored_short_reading_4: int = 0
    demoted_ascii_value: int = 0
    demoted_symbol_or_punctuation_value: int = 0
    demoted_short_kana_only_value: int = 0
    demoted_grammar_like_reading: int = 0
    demoted_orthographic_variant: int = 0
    demoted_long_tail_proper_like: int = 0
    demoted_short_name_or_title_like: int = 0
    expanded_phrase_variant_companion: int = 0
    applied_cost_floor: int = 0


URL_RE = re.compile(r"^(https?://|www\.)", re.IGNORECASE)

GRAMMAR_LIKE_READING_SUFFIXES = (
    # Particles / functional phrase tails.
    "には",
    "にも",
    "では",
    "とは",
    "から",
    "まで",
    "より",
    # Verb/adjective/auxiliary-like endings.
    "るな",
    "ない",
    "ます",
    "ました",
    "ません",
    "した",
    "して",
    "してる",
    "してた",
    "される",
    "られる",
    "たい",
    "のに",
    "ように",
)

# Orthographic variants that are valid but too aggressive as daily top
# candidates when common spellings exist.
ORTHOGRAPHIC_VARIANT_PATTERNS = (
    ("ごはん", "御飯"),
    ("ひるごはん", "昼御飯"),
    ("あちら", "彼方"),
    ("こちら", "此方"),
    ("そちら", "其方"),
    ("むげ", "無碍"),
)


def has_grammar_like_reading_suffix(key: str) -> bool:
    for suffix in GRAMMAR_LIKE_READING_SUFFIXES:
        if key.endswith(suffix) and len(key) >= len(suffix) + 2:
            return True
    return False


def is_orthographic_variant(entry: Entry) -> bool:
    for key_part, value_part in ORTHOGRAPHIC_VARIANT_PATTERNS:
        if key_part in entry.key and value_part in entry.value:
            return True
    return False


def looks_like_long_tail_proper(entry: Entry) -> bool:
    # Conservative heuristic: a generated entry whose reading looks like
    # ordinary Japanese grammar and whose value is a compact Japanese
    # name/title-like string is risky because it can consume an ordinary
    # sentence.
    if len(entry.key) < 4:
        return False
    if not has_grammar_like_reading_suffix(entry.key):
        return False
    if len(entry.value) < 3:
        return False
    return is_compact_japanese_value(entry.value)


SHORT_NAME_OR_TITLE_CHARS = frozenset("王桜姫妃丸号")


def looks_like_short_name_or_title(entry: Entry) -> bool:
    if len(entry.key) != 4:
        return False
    if len(entry.value) > 3:
        return False
    if not is_compact_japanese_value(entry.value):
        return False
    return any(ch in entry.value for ch in SHORT_NAME_OR_TITLE_CHARS)


# Full-span phrase companions for common mixed-script suffix variants.
#
# This is not a one-word patch.  The rule says:
#
#   if a generated full phrase ends with a kana-written suffix, and the base
#   dictionary has a common mixed-script spelling for that suffix, emit a
#   companion phrase with the same prefix and the mixed-script suffix.
#
# Example:
#   たなからぼたもち -> 棚からぼたもち
#   companion:          棚からぼた餅
PHRASE_SUFFIX_VARIANT_COMPANIONS = (
    ("ぼたもち", "ぼたもち", "ぼた餅"),
)


def expand_phrase_variant_companions(entry: Entry) -> list[Entry]:
    companions: list[Entry] = []

    for key_suffix, value_suffix, variant_suffix in PHRASE_SUFFIX_VARIANT_COMPANIONS:
        if not entry.key.endswith(key_suffix):
            continue
        if not entry.value.endswith(value_suffix):
            continue

        prefix = entry.value[: -len(value_suffix)]
        companion_value = prefix + variant_suffix

        if companion_value == entry.value:
            continue

        companions.append(
            Entry(
                key=entry.key,
                lid=entry.lid,
                rid=entry.rid,
                cost=min(entry.cost + 200, 20000),
                value=companion_value,
            )
        )

    return companions


def is_control_char(ch: str) -> bool:
    category = unicodedata.category(ch)
    return category.startswith("C")


def has_control_char(text: str) -> bool:
    return any(is_control_char(ch) for ch in text)


def is_symbol_or_punctuation_only(text: str) -> bool:
    has_visible = False

    for ch in text:
        if ch.isspace():
            continue

        has_visible = True
        category = unicodedata.category(ch)

        if not (category.startswith("P") or category.startswith("S")):
            return False

    return has_visible


def is_ascii_value(text: str) -> bool:
    if not text:
        return False

    return all(ord(ch) < 128 for ch in text)


def has_symbol_or_punctuation(text: str) -> bool:
    for ch in text:
        category = unicodedata.category(ch)
        if category.startswith("P") or category.startswith("S"):
            return True
    return False


def is_kana_only_value(text: str) -> bool:
    if not text:
        return False

    for ch in text:
        name = unicodedata.name(ch, "")
        if "HIRAGANA" in name or "KATAKANA" in name:
            continue
        return False

    return True


def is_compact_japanese_value(text: str) -> bool:
    if not text:
        return False

    has_japanese = False

    for ch in text:
        if ch.isspace():
            return False

        category = unicodedata.category(ch)
        if category.startswith("P") or category.startswith("S"):
            return False

        name = unicodedata.name(ch, "")
        if (
            "CJK UNIFIED IDEOGRAPH" in name
            or "HIRAGANA" in name
            or "KATAKANA" in name
        ):
            has_japanese = True
            continue

        # ASCII letters/numbers and other scripts make the entry less like a
        # compact Japanese proper-name/title candidate.
        if ord(ch) < 128:
            return False

    return has_japanese


def parse_entry(line: str, stats: Stats) -> Entry | None:
    stats.input_lines += 1

    line = line.rstrip("\n\r")
    cols = line.split("\t")

    if len(cols) != 5:
        stats.rejected_bad_columns += 1
        return None

    key, lid_text, rid_text, cost_text, value = cols

    key = unicodedata.normalize("NFKC", key.strip())
    value = unicodedata.normalize("NFKC", value.strip())

    if not key or not value:
        stats.rejected_empty += 1
        return None

    if "\ufffd" in key or "\ufffd" in value:
        stats.rejected_replacement_char += 1
        return None

    if has_control_char(key) or has_control_char(value):
        stats.rejected_control_char += 1
        return None

    if URL_RE.search(value):
        stats.rejected_url += 1
        return None

    if is_symbol_or_punctuation_only(value):
        stats.rejected_symbol_only += 1
        return None

    try:
        lid = int(lid_text)
        rid = int(rid_text)
        cost = int(cost_text)
    except ValueError:
        stats.rejected_bad_number += 1
        return None

    return Entry(key=key, lid=lid, rid=rid, cost=cost, value=value)


def adjust_cost(entry: Entry, profile: str, stats: Stats) -> Entry:
    cost = entry.cost
    key_len = len(entry.key)

    if profile == "daily":
        cost_floor = 7800

        short_reading_floor = 0

        if key_len == 1:
            cost += 2500
            short_reading_floor = 12000
            stats.demoted_short_reading_1 += 1
        elif key_len == 2:
            cost += 1200
            short_reading_floor = 10000
            stats.demoted_short_reading_2 += 1
        elif key_len == 3:
            cost += 500
            short_reading_floor = 9200
            stats.demoted_short_reading_3 += 1
        elif key_len == 4:
            short_reading_floor = 8600
            stats.demoted_short_reading_4 += 1

        if short_reading_floor and cost < short_reading_floor:
            cost = short_reading_floor
            if key_len == 1:
                stats.floored_short_reading_1 += 1
            elif key_len == 2:
                stats.floored_short_reading_2 += 1
            elif key_len == 3:
                stats.floored_short_reading_3 += 1
            elif key_len == 4:
                stats.floored_short_reading_4 += 1

        if is_ascii_value(entry.value):
            cost += 800
            stats.demoted_ascii_value += 1

        if has_symbol_or_punctuation(entry.value):
            cost += 1500
            stats.demoted_symbol_or_punctuation_value += 1

        if key_len <= 4 and is_kana_only_value(entry.value):
            cost += 1200
            stats.demoted_short_kana_only_value += 1

        # Generated daily entries whose readings look like ordinary grammar
        # should not beat natural sentence parses by default.
        if has_grammar_like_reading_suffix(entry.key):
            cost += 2500
            stats.demoted_grammar_like_reading += 1

        # Valid but uncommon orthographic variants should remain candidates,
        # but should not beat more common spellings.
        if is_orthographic_variant(entry):
            cost += 2500
            stats.demoted_orthographic_variant += 1

        # Long-tail proper-looking entries are useful for recall, but dangerous
        # when their reading is indistinguishable from an ordinary inflected
        # phrase.
        if looks_like_long_tail_proper(entry):
            cost += 3000
            stats.demoted_long_tail_proper_like += 1

        # Short generated name/title-like entries can crowd out established
        # candidates for the same reading.  Keep them as candidates, but avoid
        # letting them dominate candidate reachability.
        #
        # Example:
        #   りくおう -> 陸王 / 陸桜 / リク王
        if looks_like_short_name_or_title(entry):
            cost += 3500
            stats.demoted_short_name_or_title_like += 1

        if cost < cost_floor:
            cost = cost_floor
            stats.applied_cost_floor += 1

    elif profile == "rich":
        cost_floor = 7300

        if key_len == 1:
            cost += 1500
            stats.demoted_short_reading_1 += 1
        elif key_len == 2:
            cost += 700
            stats.demoted_short_reading_2 += 1

        if is_ascii_value(entry.value):
            cost += 400
            stats.demoted_ascii_value += 1

        if cost < cost_floor:
            cost = cost_floor
            stats.applied_cost_floor += 1

    elif profile == "max":
        pass
    else:
        raise ValueError(f"unknown profile: {profile}")

    cost = max(0, min(cost, 20000))

    if cost == entry.cost:
        return entry

    return Entry(
        key=entry.key,
        lid=entry.lid,
        rid=entry.rid,
        cost=cost,
        value=entry.value,
    )


def load_entries(path: pathlib.Path, profile: str) -> tuple[list[Entry], Stats]:
    stats = Stats()
    entries: list[Entry] = []
    seen: set[tuple[str, int, int, str]] = set()

    with path.open("r", encoding="utf-8-sig", newline="") as f:
        for line in f:
            entry = parse_entry(line, stats)
            if entry is None:
                continue

            entry = adjust_cost(entry, profile, stats)
            expanded_entries = [entry]
            expanded_entries.extend(expand_phrase_variant_companions(entry))

            for expanded_entry in expanded_entries:
                signature = (
                    expanded_entry.key,
                    expanded_entry.lid,
                    expanded_entry.rid,
                    expanded_entry.value,
                )

                if signature in seen:
                    stats.duplicates += 1
                    continue

                seen.add(signature)
                entries.append(expanded_entry)

                if expanded_entry is not entry:
                    stats.expanded_phrase_variant_companion += 1

    stats.output_lines = len(entries)
    return entries, stats


def write_entries(path: pathlib.Path, entries: Iterable[Entry]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", encoding="utf-8", newline="\n") as f:
        for entry in entries:
            f.write(
                f"{entry.key}\t{entry.lid}\t{entry.rid}\t{entry.cost}\t{entry.value}\n"
            )


def print_stats(input_path: pathlib.Path, output_path: pathlib.Path, profile: str, stats: Stats) -> None:
    print(f"Profile: {profile}")
    print(f"Input:   {input_path}")
    print(f"Output:  {output_path}")
    print("")
    print("Summary:")
    print(f"  input lines:                 {stats.input_lines}")
    print(f"  output lines:                {stats.output_lines}")
    print(f"  duplicates:                  {stats.duplicates}")
    print("")
    print("Rejected:")
    print(f"  bad columns:                 {stats.rejected_bad_columns}")
    print(f"  empty key/value:             {stats.rejected_empty}")
    print(f"  bad number:                  {stats.rejected_bad_number}")
    print(f"  control char:                {stats.rejected_control_char}")
    print(f"  replacement char:            {stats.rejected_replacement_char}")
    print(f"  symbol/punctuation only:     {stats.rejected_symbol_only}")
    print(f"  url:                         {stats.rejected_url}")
    print("")
    print("Cost adjustments:")
    print(f"  short reading len 1:         {stats.demoted_short_reading_1}")
    print(f"  short reading len 2:         {stats.demoted_short_reading_2}")
    print(f"  short reading len 3:         {stats.demoted_short_reading_3}")
    print(f"  short reading len 4:         {stats.demoted_short_reading_4}")
    print(f"  short reading floor len 1:   {stats.floored_short_reading_1}")
    print(f"  short reading floor len 2:   {stats.floored_short_reading_2}")
    print(f"  short reading floor len 3:   {stats.floored_short_reading_3}")
    print(f"  short reading floor len 4:   {stats.floored_short_reading_4}")
    print(f"  ascii value:                 {stats.demoted_ascii_value}")
    print(f"  symbol/punctuation value:    {stats.demoted_symbol_or_punctuation_value}")
    print(f"  short kana-only value:       {stats.demoted_short_kana_only_value}")
    print(f"  cost floor:                  {stats.applied_cost_floor}")
    print(f"  grammar-like reading:        {stats.demoted_grammar_like_reading}")
    print(f"  orthographic variant:        {stats.demoted_orthographic_variant}")
    print(f"  long-tail proper-like:       {stats.demoted_long_tail_proper_like}")
    print(f"  short name/title-like:       {stats.demoted_short_name_or_title_like}")
    print(f"  phrase variant companion:    {stats.expanded_phrase_variant_companion}")


def default_input_for_profile(repo_root: pathlib.Path, profile: str) -> pathlib.Path:
    generated = repo_root / "src" / "data" / "dictionary_koyasi" / "generated"

    if profile == "daily":
        return generated / "mozcdic-ut-safe.txt"

    if profile == "rich":
        return generated / "mozcdic-ut-rich.txt"

    if profile == "max":
        return generated / "mozcdic-ut-rich.txt"

    raise ValueError(f"unknown profile: {profile}")


def main() -> int:
    repo_root = pathlib.Path(__file__).resolve().parents[2]

    parser = argparse.ArgumentParser(
        description="Generate profiled Mozc system dictionary data from merge-ut output."
    )
    parser.add_argument(
        "--profile",
        choices=["daily", "rich", "max"],
        required=True,
        help="Output profile.",
    )
    parser.add_argument(
        "--input",
        type=pathlib.Path,
        default=None,
        help="Input merge-ut dictionary. Defaults to generated file for the selected profile.",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=None,
        help="Output dictionary path.",
    )

    args = parser.parse_args()

    input_path = args.input
    if input_path is None:
        input_path = default_input_for_profile(repo_root, args.profile)

    output_path = args.output
    if output_path is None:
        output_path = (
            repo_root
            / "src"
            / "data"
            / "dictionary_koyasi"
            / "generated"
            / "profiled"
            / f"mozcdic-ut-{args.profile}.txt"
        )

    if not input_path.exists():
        print(f"error: input file does not exist: {input_path}", file=sys.stderr)
        return 1

    entries, stats = load_entries(input_path, args.profile)
    write_entries(output_path, entries)
    print_stats(input_path, output_path, args.profile, stats)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
