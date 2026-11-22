#!/usr/bin/env python3
"""Offline regression tests for Nico/Pixiv source-POS mapping."""
from __future__ import annotations

import pathlib
import sys
import tempfile
import unittest

TOOLS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
from convert_nico_pixiv import (  # noqa: E402
    convert,
    cost_for,
    find_pos_id,
    parse_user_dict_line,
)


class NicoPixivPosTests(unittest.TestCase):
    def test_exact_pos_lookup(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "id.def"
            path.write_text(
                "1851 名詞,一般,*,*,*,*,*\n"
                "1920 名詞,固有名詞,一般,*,*,*,*\n"
                "1921 名詞,固有名詞,人名,一般,*,*,*\n",
                encoding="utf-8",
            )
            self.assertEqual(find_pos_id(path, "名詞,一般,*,*,*,*,*"), 1851)
            self.assertEqual(find_pos_id(path, "名詞,固有名詞,一般,*,*,*,*"), 1920)
            with self.assertRaises(RuntimeError):
                find_pos_id(path, "名詞,固有名詞,地域,一般,*,*,*")

    def test_actual_id_def(self):
        id_def = TOOLS.parents[1] / "src/data/dictionary_oss/id.def"
        self.assertEqual(find_pos_id(id_def, "名詞,一般,*,*,*,*,*"), 1851)
        self.assertEqual(find_pos_id(id_def, "名詞,固有名詞,一般,*,*,*,*"), 1920)

    def test_source_pos_changes_only_pos_not_cost_or_filtering(self):
        samples = [
            ("ひわたりかなえ", "氷渡香名江", "固有名詞"),
            ("ままかつ", "ママ活", "固有名詞"),
            ("あるふぁ", "Alpha", "アルファベット"),
            ("なぞのよみ", "未分類語", "未対応品詞"),
        ]
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            src = directory / "source.txt"
            dst = directory / "output.txt"
            src.write_text(
                "".join(f"{key}\t{value}\t{pos}\tnico-pixiv\n" for key, value, pos in samples),
                encoding="utf-8",
            )
            self.assertEqual(
                convert(
                    input_path=src,
                    output_path=dst,
                    existing_paths=[],
                    noun_general_id=1851,
                    proper_noun_id=1920,
                    base_cost=10800,
                ), 0,
            )
            lines = [line.split("\t") for line in dst.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(lines), len(samples))
            for ((key, value, pos), (out_key, lid, rid, cost, out_value)) in zip(samples, lines):
                self.assertEqual((out_key, out_value), (key, value))
                expected_id = 1920 if pos == "固有名詞" else 1851
                self.assertEqual((int(lid), int(rid)), (expected_id, expected_id))
                original = parse_user_dict_line(f"{key}\t{value}\t{pos}\tnico-pixiv")
                self.assertIsNotNone(original)
                self.assertEqual(int(cost), cost_for(original, 10800).cost)

    def test_duplicate_and_rejection_rules_still_apply(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            src = directory / "source.txt"
            existing = directory / "existing.txt"
            dst = directory / "output.txt"
            # The signature is reading+surface, not surface alone.
            existing.write_text("よみいち\t1851\t1851\t10000\t重複語\n", encoding="utf-8")
            src.write_text(
                "よみいち\t重複語\t固有名詞\tnico-pixiv\n"
                "よみふたつ\t重複語\t固有名詞\tnico-pixiv\n"
                "よみふたつ\t重複語\t固有名詞\tnico-pixiv\n"
                "てすと\t飾り☆\t固有名詞\tnico-pixiv\n",
                encoding="utf-8",
            )
            self.assertEqual(convert(src, dst, [existing], 1851, 1920, 10800), 0)
            self.assertEqual(dst.read_text(encoding="utf-8").splitlines(), [
                "よみふたつ\t1920\t1920\t10800\t重複語"
            ])


if __name__ == "__main__":
    unittest.main()
