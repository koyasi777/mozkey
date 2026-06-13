#!/usr/bin/env python3
from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import sys
import unicodedata
from collections import Counter


def configure_stdio() -> None:
    # GitHub Actions Windows runners can expose cp1252 stdout/stderr.
    # This script prints Japanese watch keys, so force UTF-8 when possible.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")


configure_stdio()


@dataclasses.dataclass(frozen=True)
class UserEntry:
    key: str
    value: str
    pos: str
    comment: str


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
    tier: str
    modifiers: tuple[str, ...]


URL_RE = re.compile(r"^(https?://|www\.)", re.IGNORECASE)

# Readings ending with these suffixes often collide with ordinary Japanese
# syntax, especially verb/adjective phrases:
#
#   とうちたいのに -> 統治体 + の + 二
#   〜したい
#   〜してる
#   〜について
#
# They should remain available, but should not beat normal sentence parses.
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

# Short contemporary compounds that users often expect as one word.
# This is intentionally pattern-based, not a one-word hand patch.
MODERN_COMPOUND_SUFFIXES = (
    "活",      # ママ活, 推し活, オタ活
    "系",      # 地雷系, 量産型系 variants
    "勢",      # ガチ勢
    "民",      # ○○民
    "厨",      # ○○厨
    "回",      # 神回
    "化",      # 擬人化, 映画化
    "沼",      # ○○沼
    "推し",    # 箱推し, 単推し
    "テロ",    # 飯テロ
)

# Values containing these characters are often phrases, meme quotes, titles
# with punctuation, or long expressive strings. They can be useful, but should
# stay weaker than compact proper nouns in the daily profile.
PHRASE_MARKER_CHARS = set(
    " \t\r\n"
    "!！?？"
    "、。，．"
    "…"
    "^~"
    "「」『』"
    "()（）"
    "[]［］"
    "{}｛｝"
    "【】"
    "<>＜＞"
    "・"
    "♪♫"
    "※"
)

TRAILING_DECORATIVE_SYMBOLS = set(
    "☆★"
    "◇◆□■△▲▽▼"
    "○●◎"
    "♡♥"
    "♪♫"
    "※"
)


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def normalize_text(s: str) -> str:
    return unicodedata.normalize("NFKC", s.strip())


def has_control_char(s: str) -> bool:
    return any(unicodedata.category(ch).startswith("C") for ch in s)


def is_symbol_only(s: str) -> bool:
    visible = False
    for ch in s:
        if ch.isspace():
            continue
        visible = True
        cat = unicodedata.category(ch)
        if not (cat.startswith("P") or cat.startswith("S")):
            return False
    return visible


def is_ascii(s: str) -> bool:
    return bool(s) and all(ord(ch) < 128 for ch in s)


def is_hiragana_text(s: str) -> bool:
    return bool(s) and all("HIRAGANA" in unicodedata.name(ch, "") for ch in s)


def is_katakana_text(s: str) -> bool:
    return bool(s) and all("KATAKANA" in unicodedata.name(ch, "") for ch in s)


def has_phrase_marker(s: str) -> bool:
    return any(ch in PHRASE_MARKER_CHARS for ch in s)


def has_trailing_decorative_symbol(s: str) -> bool:
    return bool(s) and s[-1] in TRAILING_DECORATIVE_SYMBOLS


def has_symbol_or_punctuation(s: str) -> bool:
    for ch in s:
        cat = unicodedata.category(ch)
        if cat.startswith("P") or cat.startswith("S"):
            return True
    return False


def has_dangerous_reading_suffix(key: str) -> bool:
    # Do not treat the suffix itself as dangerous. For example, "たい" alone
    # can be a legitimate short reading. The dangerous case is a longer noun
    # reading that *looks like* ordinary syntax.
    for suffix in DANGEROUS_READING_SUFFIXES:
        if key.endswith(suffix) and len(key) >= len(suffix) + 2:
            return True
    return False


def looks_like_cjk_name_or_title(value: str) -> bool:
    if not value:
        return False
    if has_phrase_marker(value):
        return False
    if has_symbol_or_punctuation(value):
        return False
    if is_ascii(value):
        return False
    # Compact CJK-heavy values from nico/pixiv are usually names, titles, or
    # long-tail proper nouns.  These are useful but should not consume ordinary
    # Japanese grammar-like readings.
    cjk_count = 0
    for ch in value:
        name = unicodedata.name(ch, "")
        if "CJK UNIFIED IDEOGRAPH" in name or "HIRAGANA" in name or "KATAKANA" in name:
            cjk_count += 1
    return len(value) >= 3 and cjk_count >= len(value) - 1


def is_modern_compound(entry: UserEntry) -> bool:
    # Compact contemporary words should be allowed to win more often:
    #   ままかつ -> ママ活
    #   おしかつ -> 推し活
    #   かみかい -> 神回
    #
    # Keep the rule fairly conservative:
    # - no ASCII value
    # - no phrase markers
    # - compact key/value
    # - known modern compound suffix
    if is_ascii(entry.value):
        return False

    if has_phrase_marker(entry.value):
        return False

    if has_symbol_or_punctuation(entry.value):
        return False

    if not (4 <= len(entry.key) <= 12):
        return False

    if not (2 <= len(entry.value) <= 10):
        return False

    return entry.value.endswith(MODERN_COMPOUND_SUFFIXES)


def is_phrase_or_meme(entry: UserEntry) -> bool:
    # Long quotes, meme phrases, and punctuation-heavy titles are useful but
    # should be weaker in daily use.
    if has_phrase_marker(entry.value):
        return True

    if len(entry.value) >= 18:
        return True

    if len(entry.key) >= 24:
        return True

    return False


def classify_entry(entry: UserEntry) -> str:
    if is_modern_compound(entry):
        return "modern_compound"

    if is_phrase_or_meme(entry):
        return "phrase_or_meme"

    if is_ascii(entry.value) or entry.pos == "アルファベット":
        return "ascii_or_alphabet"

    return "proper_noun"


def find_noun_general_id(id_def: pathlib.Path) -> int:
    with id_def.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            # Typical format starts with numeric id and then POS name.
            if "名詞,一般" not in line:
                continue

            parts = line.split()
            if not parts:
                continue

            try:
                return int(parts[0])
            except ValueError:
                continue

    raise RuntimeError(f"failed to find 名詞,一般 id in {id_def}")


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


def parse_user_dict_line(line: str) -> UserEntry | None:
    line = line.rstrip("\n\r")

    if not line:
        return None

    if line.startswith("!") or line.startswith("#"):
        return None

    cols = line.split("\t")
    if len(cols) < 2:
        return None

    key = normalize_text(cols[0])
    value = normalize_text(cols[1])
    pos = normalize_text(cols[2]) if len(cols) >= 3 else ""
    comment = normalize_text(cols[3]) if len(cols) >= 4 else ""

    if not key or not value:
        return None

    return UserEntry(key=key, value=value, pos=pos, comment=comment)


def reject_reason(entry: UserEntry) -> str | None:
    if "\ufffd" in entry.key or "\ufffd" in entry.value:
        return "replacement_char"

    if has_control_char(entry.key) or has_control_char(entry.value):
        return "control_char"

    if URL_RE.search(entry.value):
        return "url"

    if is_symbol_only(entry.value):
        return "symbol_only"

    if has_trailing_decorative_symbol(entry.value):
        return "trailing_decorative_symbol"

    return None


def cost_for(entry: UserEntry, base_cost: int) -> CostDecision:
    tier = classify_entry(entry)
    modifiers: list[str] = []

    if tier == "modern_compound":
        # Strong enough to beat common decompositions such as:
        #   まま + 勝つ
        cost = 7600

    elif tier == "phrase_or_meme":
        # Useful but should not appear too aggressively in daily conversion.
        cost = 13500

    elif tier == "ascii_or_alphabet":
        # English/ASCII names are useful, but ASCII + Japanese particles can
        # destabilize segmentation, so keep them weaker.
        cost = 11800

    elif tier == "proper_noun":
        # Normal nico/pixiv proper nouns. This is intentionally weaker than
        # typical compact daily words but not so weak that they disappear.
        cost = base_cost

    else:
        raise ValueError(f"unknown tier: {tier}")

    key_len = len(entry.key)

    if tier != "modern_compound":
        if key_len <= 2:
            cost += 4000
            modifiers.append("short_reading_len_1_2")
        elif key_len == 3:
            cost += 2500
            modifiers.append("short_reading_len_3")
        elif key_len == 4:
            cost += 1200
            modifiers.append("short_reading_len_4")

    if has_dangerous_reading_suffix(entry.key):
        cost += 4000
        modifiers.append("dangerous_reading_suffix")

        # Stronger demotion for proper-name/title-like entries whose reading looks
        # like an ordinary Japanese sentence or inflected phrase.
        if tier == "proper_noun" and looks_like_cjk_name_or_title(entry.value):
            cost += 4000
            modifiers.append("grammar_like_proper_noun")

    # Compact katakana strings generated from hiragana readings are often
    # titles, handles, or long-tail proper nouns.  They are useful as
    # candidates, but should not beat ordinary kana/idiom parses by default:
    #
    #   のるかそるか -> ノルカソルカ
    if (
        tier == "proper_noun"
        and len(entry.key) >= 5
        and is_hiragana_text(entry.key)
        and is_katakana_text(entry.value)
    ):
        cost = max(cost + 7000, 19000)
        modifiers.append("katakana_transliteration_like")

    # Extra guard for alphabet entries.
    if entry.pos == "アルファベット":
        cost += 800
        modifiers.append("alphabet_pos")

    cost = max(0, min(cost, 20000))

    return CostDecision(cost=cost, tier=tier, modifiers=tuple(modifiers))


def convert(
    input_path: pathlib.Path,
    output_path: pathlib.Path,
    existing_paths: list[pathlib.Path],
    noun_general_id: int,
    base_cost: int,
) -> int:
    existing = load_existing_signatures(existing_paths)

    stats = Counter()
    seen_new: set[tuple[str, str]] = set()
    output: list[SystemEntry] = []

    watch_keys = {
        "こう",
        "せい",
        "かん",
        "とう",
        "しょう",
        "きょう",
        "しよう",
        "かんじ",
        "にほんご",
        "とうきょう",
    }
    watch_total = Counter()
    watch_strong = Counter()
    watch_examples: dict[str, list[SystemEntry]] = {key: [] for key in watch_keys}
    strong_cost_threshold = 12500

    with input_path.open("r", encoding="utf-8-sig", errors="replace", newline="") as f:
        for line in f:
            stats["input"] += 1

            user_entry = parse_user_dict_line(line)
            if user_entry is None:
                stats["skipped_non_entry"] += 1
                continue

            reason = reject_reason(user_entry)
            if reason is not None:
                stats[f"rejected_{reason}"] += 1
                continue

            signature = (user_entry.key, user_entry.value)

            if signature in existing:
                stats["duplicate_existing"] += 1
                continue

            if signature in seen_new:
                stats["duplicate_new"] += 1
                continue

            seen_new.add(signature)

            decision = cost_for(user_entry, base_cost)
            stats[f"tier_{decision.tier}"] += 1
            for modifier in decision.modifiers:
                stats[f"modifier_{modifier}"] += 1

            system_entry = SystemEntry(
                key=user_entry.key,
                lid=noun_general_id,
                rid=noun_general_id,
                cost=decision.cost,
                value=user_entry.value,
            )

            output.append(system_entry)

            if system_entry.key in watch_keys:
                watch_total[system_entry.key] += 1

                if system_entry.cost <= strong_cost_threshold:
                    watch_strong[system_entry.key] += 1

                    if len(watch_examples[system_entry.key]) < 8:
                        watch_examples[system_entry.key].append(system_entry)

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
    print("Cost:")
    print(f"  noun_general_id: {noun_general_id}")
    print(f"  base_cost:       {base_cost}")
    print("")
    print("Tier policy:")
    print("  modern_compound:  7600")
    print("  proper_noun:      base_cost")
    print("  ascii_or_alphabet:11800")
    print("  phrase_or_meme:   13500")
    print("  dangerous suffix: +4000")
    print("  grammar-like proper noun: +4000")
    print("  katakana transliteration-like: max(+7000, 19000)")
    print("  short len <= 2:   +4000 unless modern_compound")

    print("")
    print(f"Watch keys, strong cost <= {strong_cost_threshold}:")
    for key in sorted(watch_keys):
        print(f"  {key}: total={watch_total[key]}, strong={watch_strong[key]}")

        for entry in watch_examples[key]:
            print(f"    {entry.key}\t{entry.lid}\t{entry.rid}\t{entry.cost}\t{entry.value}")

    return 0


def main() -> int:
    root = repo_root()

    parser = argparse.ArgumentParser(
        description="Convert dic-nico-intersection-pixiv Google/Mozc user dictionary to Mozc system dictionary delta."
    )
    parser.add_argument(
        "--input",
        type=pathlib.Path,
        default=root / "src" / "data" / "dictionary_koyasi" / "generated" / "nico_pixiv" / "dic-nico-intersection-pixiv-google.txt",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=root / "src" / "data" / "dictionary_koyasi" / "generated" / "profiled" / "dic-nico-pixiv-delta.txt",
    )
    parser.add_argument(
        "--daily",
        type=pathlib.Path,
        default=root / "src" / "data" / "dictionary_koyasi" / "generated" / "profiled" / "mozcdic-ut-daily.txt",
    )
    parser.add_argument(
        "--id-def",
        type=pathlib.Path,
        default=root / "src" / "data" / "dictionary_oss" / "id.def",
    )
    parser.add_argument(
        "--base-cost",
        type=int,
        default=10800,
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

    existing_paths = [args.daily]

    if not args.no_base_dictionary:
        existing_paths.extend(load_base_dictionary_paths(root))

    noun_general_id = find_noun_general_id(args.id_def)

    return convert(
        input_path=args.input,
        output_path=args.output,
        existing_paths=existing_paths,
        noun_general_id=noun_general_id,
        base_cost=args.base_cost,
    )


if __name__ == "__main__":
    raise SystemExit(main())
