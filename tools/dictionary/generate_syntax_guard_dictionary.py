#!/usr/bin/env python3
from __future__ import annotations

import argparse
import dataclasses
import pathlib
import sys
import unicodedata
from collections import Counter, defaultdict
from typing import Iterable


@dataclasses.dataclass(frozen=True)
class Entry:
    path: pathlib.Path
    line_number: int
    key: str
    lid: int
    rid: int
    cost: int
    value: str


@dataclasses.dataclass(frozen=True)
class GuardEntry:
    key: str
    lid: int
    rid: int
    cost: int
    value: str
    reason: str
    source_key: str
    source_value: str
    collision_value: str


PREFIX_GUARDS = (
    "と",
    "に",
)

FUNCTION_PHRASE_GUARDS = (
    ("のに", "のに", "の", "に"),
    ("には", "には", "に", "は"),
    ("では", "では", "で", "は"),
    ("とは", "とは", "と", "は"),
    ("にも", "にも", "に", "も"),
    ("にまで", "にまで", "に", "まで"),
    ("でも", "でも", "で", "も"),
    ("よりも", "よりも", "より", "も"),
    ("までに", "までに", "まで", "に"),
    ("までも", "までも", "まで", "も"),
    ("について", "について", "に", "て"),
    ("として", "として", "と", "て"),
)

SEEDED_PREFIX_GUARDS = (
    # Fix:
    #   とうちたいのに -> 統治体の二
    ("とうちたい", "と打ちたい", "と", "うちたい"),

    # Fix:
    #   subcultureに分ける -> subculture二分ける
    ("にわける", "に分ける", "に", "わける"),

    # Secondary useful variant.
    ("にわける", "に別ける", "に", "わける"),

    # Fix:
    #   になったりします -> 担ったりします
    # Keep the grammar boundary between the particle に and なったり...
    ("になったり", "になったり", "に", "なったり"),
    ("になったりする", "になったりする", "に", "なったりする"),
    ("になったりします", "になったりします", "に", "なったりします"),
    ("になったりして", "になったりして", "に", "なったりして"),
    ("になったりした", "になったりした", "に", "なったりした"),

    # Fix:
    #   になってしまいます -> 担ってしまいます
    # Keep the grammar boundary between the particle に and the auxiliary-like
    # なってしま... sequence.  Do not add bare になって here; it can legitimately
    # compete with 担って.  Guard only longer constructions where the functional
    # phrase reading is much more likely.
    ("になってしま", "になってしま", "に", "なってしま"),
    ("になってしまう", "になってしまう", "に", "なってしまう"),
    ("になってしまい", "になってしまい", "に", "なってしまい"),
    ("になってしまいます", "になってしまいます", "に", "なってしまいます"),
    ("になってしまいました", "になってしまいました", "に", "なってしまいました"),
    ("になってしまっ", "になってしまっ", "に", "なってしまっ"),
    ("になってしまった", "になってしまった", "に", "なってしまった"),
    ("になってしまって", "になってしまって", "に", "なってしまって"),
    ("になってしまえば", "になってしまえば", "に", "なってしまえば"),
    ("になってしまわ", "になってしまわ", "に", "なってしまわ"),
    ("になってしまわない", "になってしまわない", "に", "なってしまわない"),
    ("になってしまわず", "になってしまわず", "に", "なってしまわず"),

    # Colloquial contraction of になってしま...
    ("になっちゃ", "になっちゃ", "に", "なっちゃ"),
    ("になっちゃう", "になっちゃう", "に", "なっちゃう"),
    ("になっちゃい", "になっちゃい", "に", "なっちゃい"),
    ("になっちゃいます", "になっちゃいます", "に", "なっちゃいます"),
    ("になっちゃった", "になっちゃった", "に", "なっちゃった"),
    ("になっちゃって", "になっちゃって", "に", "なっちゃって"),
)

SEEDED_FIXED_GUARDS = (
    # existing entries...

    # Fix:
    # むげにはできない -> 無碍にはできない
    ("むげにはできない", "無下にはできない", "には", "できない"),

    # Fix:
    # きりがない -> 霧がない
    ("きりがない", "きりがない", "きり", "ない"),

    # Fix:
    # もとにもどす -> 下に戻す
    ("もとにもどす", "元に戻す", "に", "もどす"),

    # Fix:
    # もとにもどして -> 下に戻して
    ("もとにもどして", "元に戻して", "に", "もどして"),

    # Fix:
    # とうちたいのに -> 統治体の二
    ("とうちたいのに", "と打ちたいのに", "と", "のに"),

    # Fix:
    # したほうが... -> 下ほうが... / 下方が...
    # 方が is a formal-noun + case-particle construction following a verbal
    # expression.  Guard this grammar boundary once, so it can connect to many
    # following predicates such as いい, 悪い, 早い, 自然, 安全, and マシ.
    # Do not make bare した too cheap; keep the rescue at the longer
    # functional boundary.
    ("したほうが", "した方が", "した", "が"),

    # Fix:
    # したにもかかわらず -> 下にもかかわらず
    # にもかかわらず is a concessive functional expression.  In this full
    # sequence, verbal した is overwhelmingly more natural than noun 下.
    ("したにもかかわらず", "したにもかかわらず", "した", "ず"),
)

DIRECT_FIXED_GUARDS = (
    # Idiom rescue:
    # 肌身離さず is a fixed idiomatic expression.  This is not a broad
    # syntax-boundary guard; keep it explicit so generation remains
    # reproducible without depending on whether component entries exist.
    ("はだみはなさ", 1851, 1851, 3600, "肌身離さ", "direct_idiom_guard"),
    ("はだみはなさず", 1851, 1851, 3600, "肌身離さず", "direct_idiom_guard"),

)

# Direct kana entries for completed expressive words.
#
# These entries are intentionally dictionary candidates, not session-level
# live-conversion suppressors.  This lets the default output rescue natural
# hiragana forms while still allowing user history / user dictionary entries
# such as ウッソ, クッソ, ヤッバ, etc. to win after explicit selection.
#
# Use conservative costs close to the existing hiragana mimetic rescue entries:
# cheap enough to beat pathological kanji segmentation, but not so cheap that
# user-learned katakana spellings become unreachable.
DEFAULT_EXPRESSIVE_KANA_GUARD_COSTS = (
    (12, 12, 4300),
    (1841, 1841, 6200),
)

# These forms strongly compete with common katakana loanword candidates such as
# エッグ.  Keep them as dictionary candidates, not session suppressions, but make
# the default hiragana rescue slightly stronger than the generic expressive
# kana guards.
STRONG_EXPRESSIVE_KANA_GUARD_COSTS = (
    (12, 12, 3600),
    (1841, 1841, 5600),
)

# Some expressive spellings compete with very strong ordinary dictionary
# entries after the converter normalizes or approximates repeated small っ.
# Keep these as dictionary candidates rather than session suppressions, but make
# the kana rescue strong enough to beat the ordinary homophone path.
EXTRA_STRONG_EXPRESSIVE_KANA_GUARD_COSTS = (
    (12, 12, 2600),
    (1841, 1841, 4600),
)

EXPRESSIVE_KANA_COST_OVERRIDES = {
    "えっぐ": STRONG_EXPRESSIVE_KANA_GUARD_COSTS,
    "えっっぐ": STRONG_EXPRESSIVE_KANA_GUARD_COSTS,
    "えっぐい": STRONG_EXPRESSIVE_KANA_GUARD_COSTS,
    "えっっぐい": STRONG_EXPRESSIVE_KANA_GUARD_COSTS,

    # Do not add bare あっか because it strongly conflicts with 悪化.  Only the
    # explicitly emphasized double-促音 spelling is rescued here.
    "あっっか": EXTRA_STRONG_EXPRESSIVE_KANA_GUARD_COSTS,
}


def make_direct_expressive_kana_guards(*surface_groups):
    surfaces = []
    seen = set()
    for group in surface_groups:
        for surface in group:
            if surface in seen:
                continue
            seen.add(surface)
            surfaces.append(surface)

    guards = []
    for surface in surfaces:
        for lid, rid, cost in EXPRESSIVE_KANA_COST_OVERRIDES.get(
            surface, DEFAULT_EXPRESSIVE_KANA_GUARD_COSTS
        ):
            guards.append(
                (surface, lid, rid, cost, surface, "direct_expressive_kana_guard")
            )
    return tuple(guards)


# Short utterances and particles are intentionally kept as plain dictionary
# candidates here, not as hard session-level suppressions.  Some of these are
# short and ambiguous, but adding a kana candidate is still less invasive than
# preventing the converter, user history, and user dictionary from participating.
EXPRESSIVE_KANA_SHORT_UTTERANCE_SURFACES = (
    "ふん",
    "ふんっ",
    "ふんっっ",
    "ふんっっっー",
    "ふむ",
    "ふむっ",
    "はて",
    "ほう",
    "ほうっっ",
    "ちっ",
    "ちぇっ",
    "くそ",
    "よう",
    "ほっ",
    "ふう",
    "いてっ",
    "ぎゃっ",
    "ひゃっ",
    "はいはい",
    "ほい",
    "ほいっ",
    "ほいっっ",
    "ほいほい",
    "ほいほいっ",
    "へい",
    "へいっ",
    "へいっっ",
    "しゃっ",
    "しゃっっ",
    "しゃっー",
    "てへ",
    "てへっ",
    "てへっっ",
    "ええ",
    "えぇ",
    "ぇぇ",
    "えぇぇ",
    "えぇー",
)


EXPRESSIVE_KANA_MIMETIC_SURFACES = (
    "くちゃ",
    "くちょ",
    "ぐちょ",
    "ぐちょっ",
    "ぐちょっっ",
    "ぐちょぐちょ",
    "ぐちょぐちょっ",
    "ぐちょっと",
    "どろっ",
    "どろっっ",
    "どろっー",
    "どろどろ",
    "どろどろっ",
    "とろっ",
    "とろっっ",
    "とろっー",
    "さわっ",
    "さわっっ",
    "さわっー",
    "つるっ",
    "つるっっ",
    "つるっー",
    "つるつる",
    "つるつるっ",
)


EXPRESSIVE_KANA_CASUAL_GREETING_SURFACES = (
    "ちっす",
    "ちっっす",
    "ちーっす",
    "ちぃーっす",
    "ちぃーーっす",
    "ちーっすー",
    "ちょっす",
    "ちょーっす",
    "ちょーっすー",
    "ちょりっす",
    "ちょりーっす",
    "ちょりーっすー",
)


EXPRESSIVE_KANA_UHYO_UHYA_SURFACES = (
    "うひょ",
    "うひょー",
    "うひょーん",
    "うっひょ",
    "うっひょー",
    "うっひょーん",
    "うっっひょ",
    "うっっひょーん",
    "うひゃ",
    "うひゃー",
    "うひゃ～",
    "うひゃーん",
    "うっひゃ",
    "うっひゃー",
    "うっひゃーん",
    "うっっひゃ",
    "うっっひゃーん",
    "ほほう",
    "ほっほーん",
)


EXPRESSIVE_KANA_USO_KUSO_SURFACES = (
    "うっそ",
    "うっそー",
    "うっそん",
    "うっっそ",
    "うっっっそーん",
    "くっそ",
    "くっそー",
    "くっっそ",
    "くっっっそー",
)


EXPRESSIVE_KANA_EVALUATIVE_SLANG_SURFACES = (
    "やっば",
    "やっっばい",
    "やっべぇ",
    "やっっべー",
    "すっご",
    "すっっごい",
    "すっげぇ",
    "すっっげー",
    "すっくな",
    "すっっくない",
    "すっくね",
    "すっっくねぇ",
    "すっくねえ",
    "すっくねー",
    "すっぱ",
    "すっっぱ",
    "すっっっぱ",
    "すっぱー",
    "すっっぱー",
    "すっぺぇ",
    "すっっぺー",
    "こっわ",
    "こっっわい",
    "つっよ",
    "つっっよい",
    "つっら",
    "つっっらい",
    "つっれぇ",
    "つっっれー",
    "でっか",
    "でっっかい",
    "なっが",
    "なっっがい",
    "たっか",
    "たっっかい",
    "たっけぇ",
    "たっっけー",
    "ちっさ",
    "ちっっさい",
    "ちっちゃ",
    "ちっっちゃい",
    "ちっせぇ",
    "ちっっせー",
    "ひっく",
    "ひっっくい",
    "ひっろ",
    "ひっっろい",
    "さっむ",
    "さっっむい",
    "さっみぃ",
    "さっみい",
    "さっっみー",
    "さっっみい",
    "あっつ",
    "あっっつい",
    "あっちぃ",
    "あっちい",
    "あっっちー",
    "あっっちい",
    "あっっか",
    "うっま",
    "うっっまい",
    "うっめぇ",
    "うっっめー",
    "うっざ",
    "うっっざい",
    "うっぜぇ",
    "うっっぜー",
    "うっす",
    "うっっすい",
    "かっる",
    "かっっるい",
    "きっつ",
    "きっっつい",
    "きっちぃ",
    "きっっちー",
    "きっも",
    "きっっもい",
    "きっれい",
    "きっっれい",
    "だっる",
    "だっっるい",
    "だっりぃ",
    "だっっりー",
    "だっさ",
    "だっっさい",
    "だっせぇ",
    "だっっせー",
    "えっぐ",
    "えっっぐ",
    "えっぐい",
    "えっっぐい",
    "くっさ",
    "くっっさい",
    "くっせ",
    "くっっせ",
    "くっせぇ",
    "くっっせー",
    "くっろ",
    "くっっろい",
    "まっぶし",
    "まっっぶしい",
    "おっも",
    "おっっもい",
    "おっそ",
    "おっっそい",
    "はっや",
    "はっっやい",
    "めっちゃ",
    "めっっちゃ",
    "もっと",
    "もっっと",
    "もっっっと",
    "もっとー",
    "もっっとー",
    "ねっむ",
    "ねっっむい",
    "ほっそ",
    "ほっっそい",
    "せっま",
    "せっっまい",
    "みっじか",
    "みっっじかい",
    "しっろ",
    "しっっろい",
)


# Surprise / wonder interjections such as ほえー and ほぇ～.
#
# Do not add bare ほえ here because it can compete with 吠え.  Add only clearly
# expressive forms with vowel reduction, prolongation, or decorative tails.
#
# Keep both ～ U+FF5E and 〜 U+301C variants.  They are visually similar but can
# appear through different input paths, and direct guards are written as literal
# dictionary entries.
EXPRESSIVE_KANA_HOE_SURFACES = (
    "ほえー",
    "ほえ～",
    "ほぇ",
    "ほぇー",
    "ほぇ～",
    "ほえぇ",
    "ほえぇー",
    "ほえぇ～",
    "ほぇぇ",
    "ほぇぇー",
    "ほぇぇ～",
    "ほえ〜",
    "ほぇ〜",
    "ほえぇ〜",
    "ほぇぇ〜",
)


DIRECT_EXPRESSIVE_KANA_GUARDS = make_direct_expressive_kana_guards(
    EXPRESSIVE_KANA_SHORT_UTTERANCE_SURFACES,
    EXPRESSIVE_KANA_MIMETIC_SURFACES,
    EXPRESSIVE_KANA_CASUAL_GREETING_SURFACES,
    EXPRESSIVE_KANA_UHYO_UHYA_SURFACES,
    EXPRESSIVE_KANA_USO_KUSO_SURFACES,
    EXPRESSIVE_KANA_EVALUATIVE_SLANG_SURFACES,
    EXPRESSIVE_KANA_HOE_SURFACES,
)

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
    "たい",
    "ない",
    "ます",
    "ってる",
    "いてる",
    "いでる",
    "してる",
    "てる",
    "でる",
    "れる",
    "のに",
)

GRAMMAR_SUFFIX_PAIRS = (
    ("したい", ("したい",)),
    ("いたい", ("いたい",)),
    ("きたい", ("きたい",)),
    ("ぎたい", ("ぎたい",)),
    ("ちたい", ("ちたい",)),
    ("りたい", ("りたい",)),
    ("たい", ("たい",)),

    ("られない", ("られない",)),
    ("れない", ("れない",)),
    ("せない", ("せない",)),
    ("ない", ("ない",)),

    ("ました", ("ました",)),
    ("ません", ("ません",)),
    ("ます", ("ます",)),

    ("ってる", ("ってる",)),
    ("いてる", ("いてる",)),
    ("いでる", ("いでる",)),
    ("してる", ("してる",)),
    ("てる", ("てる",)),
    ("でる", ("でる",)),

    ("られる", ("られる",)),
    ("される", ("される",)),
    ("れる", ("れる",)),
)


def repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def normalize_text(text: str) -> str:
    return unicodedata.normalize("NFKC", text.strip())


def is_hiragana(ch: str) -> bool:
    return "\u3040" <= ch <= "\u309f"


def is_katakana(ch: str) -> bool:
    return "\u30a0" <= ch <= "\u30ff" or "\uff66" <= ch <= "\uff9f"


def is_kanji(ch: str) -> bool:
    return (
        "\u3400" <= ch <= "\u4dbf"
        or "\u4e00" <= ch <= "\u9fff"
        or "\uf900" <= ch <= "\ufaff"
        or ch in "々〆ヶ"
    )


def has_hiragana(text: str) -> bool:
    return any(is_hiragana(ch) for ch in text)


def has_katakana(text: str) -> bool:
    return any(is_katakana(ch) for ch in text)


def has_kanji(text: str) -> bool:
    return any(is_kanji(ch) for ch in text)


def is_ascii_only(text: str) -> bool:
    return bool(text) and all(ord(ch) < 128 for ch in text)


def has_control_or_space(text: str) -> bool:
    return any(ch.isspace() or unicodedata.category(ch).startswith("C") for ch in text)


def is_symbol_or_punctuation(ch: str) -> bool:
    category = unicodedata.category(ch)
    return category.startswith("P") or category.startswith("S")


def has_symbol_or_punctuation(text: str) -> bool:
    return any(is_symbol_or_punctuation(ch) for ch in text)


def parse_entry(path: pathlib.Path, line_number: int, line: str) -> Entry | None:
    cols = line.rstrip("\n\r").split("\t")
    if len(cols) != 5:
        return None

    key, lid_text, rid_text, cost_text, value = cols

    try:
        return Entry(
            path=path,
            line_number=line_number,
            key=normalize_text(key),
            lid=int(lid_text),
            rid=int(rid_text),
            cost=int(cost_text),
            value=normalize_text(value),
        )
    except ValueError:
        return None


def default_dictionary_paths(root: pathlib.Path) -> list[pathlib.Path]:
    dictionary_oss = root / "src" / "data" / "dictionary_oss"
    return sorted(dictionary_oss.glob("dictionary[0-9][0-9].txt"))


def load_entries(paths: Iterable[pathlib.Path]) -> list[Entry]:
    entries: list[Entry] = []

    for path in paths:
        if not path.exists():
            print(f"warning: dictionary does not exist: {path}", file=sys.stderr)
            continue

        with path.open("r", encoding="utf-8-sig", errors="replace", newline="") as f:
            for line_number, line in enumerate(f, start=1):
                entry = parse_entry(path, line_number, line)
                if entry is None:
                    continue
                entries.append(entry)

    return entries


def best_exact_value_entries(entries: Iterable[Entry]) -> dict[str, Entry]:
    best: dict[str, Entry] = {}

    for entry in entries:
        if entry.key != entry.value:
            continue

        current = best.get(entry.key)
        if current is None or entry.cost < current.cost:
            best[entry.key] = entry

    return best


def dangerous_suffix_for_key(key: str) -> str | None:
    for suffix in DANGEROUS_READING_SUFFIXES:
        if key.endswith(suffix) and len(key) >= len(suffix) + 2:
            return suffix
    return None


def matched_grammar_suffix(entry: Entry) -> str | None:
    for key_suffix, value_suffixes in GRAMMAR_SUFFIX_PAIRS:
        if not entry.key.endswith(key_suffix):
            continue

        if len(entry.key) < len(key_suffix) + 1:
            continue

        if any(entry.value.endswith(value_suffix) for value_suffix in value_suffixes):
            return key_suffix

    return None


def looks_like_safe_grammar_source(entry: Entry, max_source_cost: int) -> bool:
    if entry.cost > max_source_cost:
        return False

    if not entry.key or not entry.value:
        return False

    if len(entry.key) > 16:
        return False

    if is_ascii_only(entry.value):
        return False

    if has_control_or_space(entry.value):
        return False

    if has_symbol_or_punctuation(entry.value):
        return False

    # Natural grammar source must contain hiragana in the surface.
    # This excludes pure nouns like 統治体.
    if not has_hiragana(entry.value):
        return False

    # Avoid katakana proper nouns.
    if has_katakana(entry.value):
        return False

    # Prefer kanji+kana grammar expressions.
    # Pure hiragana grammar expressions are allowed only if cheap.
    if not has_kanji(entry.value) and entry.cost > 2500:
        return False

    if matched_grammar_suffix(entry) is None:
        return False

    return True


def looks_like_collision_candidate(entry: Entry, max_collision_cost: int) -> bool:
    if entry.cost > max_collision_cost:
        return False

    if dangerous_suffix_for_key(entry.key) is None:
        return False

    # Collision value should be noun-like / non-grammar-like.
    # Natural grammar expressions contain hiragana, e.g. 行きたい, 愛してる.
    if has_hiragana(entry.value):
        return False

    if is_ascii_only(entry.value):
        return False

    if has_control_or_space(entry.value):
        return False

    # We mainly care about kanji/kana noun surfaces like 統治体, 無敵艦隊.
    if not (has_kanji(entry.value) or has_katakana(entry.value)):
        return False

    return True


def make_prefix_guard(
    prefix: str,
    prefix_entry: Entry,
    source: Entry,
    collision: Entry,
    guard_cost: int,
) -> GuardEntry:
    return GuardEntry(
        key=f"{prefix}{source.key}",
        lid=prefix_entry.lid,
        rid=source.rid,
        cost=guard_cost,
        value=f"{prefix}{source.value}",
        reason="collision_driven_prefix_guard",
        source_key=source.key,
        source_value=source.value,
        collision_value=collision.value,
    )


def make_function_phrase_guard(
    key: str,
    value: str,
    left_entry: Entry,
    right_entry: Entry,
    guard_cost: int,
) -> GuardEntry:
    return GuardEntry(
        key=key,
        lid=left_entry.lid,
        rid=right_entry.rid,
        cost=guard_cost,
        value=value,
        reason="function_phrase_guard",
        source_key=key,
        source_value=value,
        collision_value="",
    )


def make_seeded_prefix_guard(
    key: str,
    value: str,
    prefix_key: str,
    source_key: str,
    prefix_entry: Entry,
    fallback_right_entry: Entry,
    guard_cost: int,
) -> GuardEntry:
    return GuardEntry(
        key=key,
        lid=prefix_entry.lid,
        rid=fallback_right_entry.rid,
        cost=guard_cost,
        value=value,
        reason="seeded_prefix_guard",
        source_key=source_key,
        source_value=value,
        collision_value="",
    )


def make_seeded_fixed_guard(
    key: str,
    value: str,
    left_entry: Entry,
    right_entry: Entry,
    guard_cost: int,
) -> GuardEntry:
    return GuardEntry(
        key=key,
        lid=left_entry.lid,
        rid=right_entry.rid,
        cost=guard_cost,
        value=value,
        reason="seeded_fixed_guard",
        source_key=key,
        source_value=value,
        collision_value="",
    )


def make_direct_fixed_guard(
    key: str,
    lid: int,
    rid: int,
    cost: int,
    value: str,
    reason: str,
) -> GuardEntry:
    return GuardEntry(
        key=key,
        lid=lid,
        rid=rid,
        cost=cost,
        value=value,
        reason=reason,
        source_key=key,
        source_value=value,
        collision_value="",
    )


SEEDED_PREFIX_GUARD_COST_OVERRIDES = {
    # These guards compete with strong ordinary verb candidates such as
    # 担ってしまいます. Keep the left id inherited from the particle に so the
    # guard still prefers noun/adjectival contexts before に, but make the guard
    # cost strong enough to beat the content-verb path in 名詞 + になってしま...
    # constructions.
    "になってしま": 2400,
    "になってしまう": 2400,
    "になってしまい": 2400,
    "になってしまいます": 2400,
    "になってしまいました": 2400,
    "になってしまっ": 2400,
    "になってしまった": 2400,
    "になってしまって": 2400,
    "になってしまえば": 2400,
    "になってしまわ": 2400,
    "になってしまわない": 2400,
    "になってしまわず": 2400,
    "になっちゃ": 2400,
    "になっちゃう": 2400,
    "になっちゃい": 2400,
    "になっちゃいます": 2400,
    "になっちゃった": 2400,
    "になっちゃって": 2400,
}


def generate_guards(
    entries: list[Entry],
    max_source_cost: int,
    max_collision_cost: int,
    prefix_guard_cost: int,
    function_guard_cost: int,
) -> tuple[list[GuardEntry], Counter]:
    stats = Counter()
    exact_entries = best_exact_value_entries(entries)

    grammar_sources_by_key: dict[str, list[Entry]] = defaultdict(list)

    for entry in entries:
        if not looks_like_safe_grammar_source(entry, max_source_cost):
            continue
        grammar_sources_by_key[entry.key].append(entry)
        stats["grammar_sources"] += 1

    for key in list(grammar_sources_by_key.keys()):
        grammar_sources_by_key[key].sort(key=lambda e: (e.cost, e.value, e.lid, e.rid))

    guards: list[GuardEntry] = []
    seen: set[tuple[str, int, int, str]] = set()

    for collision in entries:
        if not looks_like_collision_candidate(collision, max_collision_cost):
            continue

        stats["collision_candidates"] += 1

        for prefix in PREFIX_GUARDS:
            if not collision.key.startswith(prefix):
                continue

            rest_key = collision.key[len(prefix):]
            if not rest_key:
                continue

            sources = grammar_sources_by_key.get(rest_key)
            if not sources:
                stats[f"missing_source_for_prefix_{prefix}"] += 1
                continue

            prefix_entry = exact_entries.get(prefix)
            if prefix_entry is None:
                stats[f"missing_prefix_{prefix}"] += 1
                continue

            # Use only a few best grammar sources per collision to avoid bloat.
            for source in sources[:3]:
                guard = make_prefix_guard(
                    prefix=prefix,
                    prefix_entry=prefix_entry,
                    source=source,
                    collision=collision,
                    guard_cost=prefix_guard_cost,
                )

                signature = (guard.key, guard.lid, guard.rid, guard.value)
                if signature in seen:
                    stats["duplicate_guard"] += 1
                    continue

                seen.add(signature)
                guards.append(guard)
                stats["prefix_guards"] += 1

    for key, value, left_key, right_key in FUNCTION_PHRASE_GUARDS:
        left_entry = exact_entries.get(left_key)
        right_entry = exact_entries.get(right_key)

        if left_entry is None or right_entry is None:
            stats["missing_function_phrase_part"] += 1
            continue

        guard = make_function_phrase_guard(
            key=key,
            value=value,
            left_entry=left_entry,
            right_entry=right_entry,
            guard_cost=function_guard_cost,
        )

        signature = (guard.key, guard.lid, guard.rid, guard.value)
        if signature in seen:
            stats["duplicate_guard"] += 1
            continue

        seen.add(signature)
        guards.append(guard)
        stats["function_phrase_guards"] += 1

    # Add a very small set of hand-picked grammar boundary guards.
    # These are not broad dictionary additions; they are syntax boundary
    # protectors for known high-impact segmentation failures.
    for key, value, prefix_key, source_key in SEEDED_PREFIX_GUARDS:
        prefix_entry = exact_entries.get(prefix_key)
        if prefix_entry is None:
            stats["missing_seed_prefix"] += 1
            continue

        # Prefer a real dictionary entry for the source key if available.
        source_candidates = grammar_sources_by_key.get(source_key)
        if source_candidates:
            fallback_right_entry = source_candidates[0]
        else:
            # Fall back to the prefix entry only to keep generation robust.
            # The guard still works as a system dictionary entry, but this is
            # less ideal than using the real source expression's right id.
            fallback_right_entry = prefix_entry
            stats["seed_used_prefix_as_fallback_rid"] += 1

        guard = make_seeded_prefix_guard(
            key=key,
            value=value,
            prefix_key=prefix_key,
            source_key=source_key,
            prefix_entry=prefix_entry,
            fallback_right_entry=fallback_right_entry,
            guard_cost=SEEDED_PREFIX_GUARD_COST_OVERRIDES.get(
                key, prefix_guard_cost
            ),
        )

        signature = (guard.key, guard.lid, guard.rid, guard.value)
        if signature in seen:
            stats["duplicate_guard"] += 1
            continue

        seen.add(signature)
        guards.append(guard)
        stats["seeded_prefix_guards"] += 1

    for key, value, left_key, right_key in SEEDED_FIXED_GUARDS:
        left_entry = exact_entries.get(left_key)
        right_entry = exact_entries.get(right_key)

        if left_entry is None or right_entry is None:
            stats["missing_seeded_fixed_part"] += 1
            continue

        guard = make_seeded_fixed_guard(
            key=key,
            value=value,
            left_entry=left_entry,
            right_entry=right_entry,
            guard_cost=function_guard_cost,
        )

        signature = (guard.key, guard.lid, guard.rid, guard.value)
        if signature in seen:
            stats["duplicate_guard"] += 1
            continue

        seen.add(signature)
        guards.append(guard)
        stats["seeded_fixed_guards"] += 1

    for key, lid, rid, cost, value, reason in (
        DIRECT_FIXED_GUARDS + DIRECT_EXPRESSIVE_KANA_GUARDS
    ):
        guard = make_direct_fixed_guard(
            key=key,
            lid=lid,
            rid=rid,
            cost=cost,
            value=value,
            reason=reason,
        )
        signature = (guard.key, guard.lid, guard.rid, guard.value)
        if signature in seen:
            stats["duplicate_guard"] += 1
            continue
        seen.add(signature)
        guards.append(guard)
        stats[reason] += 1

    guards.sort(key=lambda e: (e.key, e.value, e.lid, e.rid, e.cost))

    return guards, stats


def write_guards(path: pathlib.Path, guards: Iterable[GuardEntry]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", encoding="utf-8", newline="\n") as f:
        for entry in guards:
            f.write(f"{entry.key}\t{entry.lid}\t{entry.rid}\t{entry.cost}\t{entry.value}\n")


def write_debug_tsv(path: pathlib.Path, guards: Iterable[GuardEntry]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write(
            "key\tlid\trid\tcost\tvalue\treason\tsource_key\tsource_value\tcollision_value\n"
        )
        for entry in guards:
            f.write(
                f"{entry.key}\t{entry.lid}\t{entry.rid}\t{entry.cost}\t"
                f"{entry.value}\t{entry.reason}\t{entry.source_key}\t"
                f"{entry.source_value}\t{entry.collision_value}\n"
            )


def main() -> int:
    root = repo_root()

    parser = argparse.ArgumentParser(
        description="Generate a collision-driven syntax guard dictionary from base Mozc dictionaries."
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=root
        / "src"
        / "data"
        / "dictionary_koyasi"
        / "generated"
        / "profiled"
        / "koyasi-syntax-guard.txt",
    )
    parser.add_argument(
        "--debug-tsv",
        type=pathlib.Path,
        default=root
        / "dist"
        / "dictionary"
        / "koyasi-syntax-guard-debug.tsv",
    )
    parser.add_argument(
        "--max-source-cost",
        type=int,
        default=9000,
        help="Only source natural grammar entries with cost <= this value.",
    )
    parser.add_argument(
        "--max-collision-cost",
        type=int,
        default=8500,
        help="Only suspicious collision entries with cost <= this value.",
    )
    parser.add_argument(
        "--prefix-guard-cost",
        type=int,
        default=3600,
        help="Cost for generated prefix+grammar guard entries.",
    )
    parser.add_argument(
        "--function-guard-cost",
        type=int,
        default=200,
        help="Cost for fixed function phrase guards such as のに.",
    )
    parser.add_argument(
        "--path",
        type=pathlib.Path,
        action="append",
        default=None,
        help="Dictionary path to read. Can be passed multiple times.",
    )

    args = parser.parse_args()

    paths = args.path if args.path else default_dictionary_paths(root)
    entries = load_entries(paths)
    guards, stats = generate_guards(
        entries=entries,
        max_source_cost=args.max_source_cost,
        max_collision_cost=args.max_collision_cost,
        prefix_guard_cost=args.prefix_guard_cost,
        function_guard_cost=args.function_guard_cost,
    )

    write_guards(args.output, guards)
    write_debug_tsv(args.debug_tsv, guards)

    print("Generated syntax guard dictionary:")
    print(f"  output:    {args.output}")
    print(f"  debug tsv: {args.debug_tsv}")
    print("")
    print("Stats:")
    print(f"  input entries:            {len(entries)}")
    for key in sorted(stats.keys()):
        print(f"  {key}: {stats[key]}")
    print(f"  output guards:            {len(guards)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())