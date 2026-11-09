"""Regression tests for generated Mozkey OSS macOS postflight filtering."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import postflight_mozkey_oss_filter as filter_script


def expected_base():
    return (
        b'#!/bin/sh\n'
        + b''.join(filter_script.KEEP_LINES)
        + filter_script.UNSAFE_LINE
        + b'/usr/bin/true\n'
    )


def expected_tail():
    return filter_script.TAIL_MARKER + b'\nexit 0\n'


class PostflightSafetyTest(unittest.TestCase):
    def test_removes_only_generic_llama_killall(self):
        output = filter_script.transform(expected_base(), expected_tail())
        self.assertNotIn(filter_script.UNSAFE_LINE, output)
        self.assertTrue(output.startswith(b'#!/bin/sh\n'))
        self.assertEqual(output.count(filter_script.TAIL_MARKER), 1)
        for line in filter_script.KEEP_LINES:
            self.assertEqual(output.count(line), 1)

    def test_missing_generic_killall_fails_closed(self):
        bad = expected_base().replace(filter_script.UNSAFE_LINE, b'')
        with self.assertRaisesRegex(ValueError, 'UNSAFE_LLAMA_KILLALL_NOT_EXACTLY_ONCE'):
            filter_script.transform(bad, expected_tail())

    def test_duplicate_generic_killall_fails_closed(self):
        bad = expected_base() + filter_script.UNSAFE_LINE
        with self.assertRaisesRegex(ValueError, 'UNSAFE_LLAMA_KILLALL_NOT_EXACTLY_ONCE'):
            filter_script.transform(bad, expected_tail())

    def test_missing_required_mozc_process_line_fails_closed(self):
        bad = expected_base().replace(filter_script.KEEP_LINES[0], b'')
        with self.assertRaisesRegex(ValueError, 'EXPECTED_MOZC_PROCESS_LINES_CHANGED'):
            filter_script.transform(bad, expected_tail())

    def test_additional_llama_server_kill_fails_closed(self):
        bad = expected_base() + b'pkill -f llama-server\n'
        with self.assertRaisesRegex(ValueError, 'UNSAFE_LLAMA_SERVER_STOP_REMAINS'):
            filter_script.transform(bad, expected_tail())

    def test_duplicate_tail_rejected(self):
        bad = expected_base() + filter_script.TAIL_MARKER + b'\n'
        with self.assertRaisesRegex(ValueError, 'UNEXPECTED_PREMATURE_EXIT_OR_DUPLICATE_TAIL'):
            filter_script.transform(bad, expected_tail())

    def test_invalid_tail_rejected(self):
        with self.assertRaisesRegex(ValueError, 'UNEXPECTED_OSS_CLEANUP_TAIL_FORMAT'):
            filter_script.transform(expected_base(), b'#!/bin/sh\nexit 0\n')

    def test_premature_exit_rejected(self):
        bad = expected_base() + b'exit 0\n'
        with self.assertRaisesRegex(ValueError, 'UNEXPECTED_PREMATURE_EXIT_OR_DUPLICATE_TAIL'):
            filter_script.transform(bad, expected_tail())

    def test_crlf_rejected(self):
        with self.assertRaisesRegex(ValueError, 'UNEXPECTED_GENERATED_POSTFLIGHT_FORMAT'):
            filter_script.transform(expected_base().replace(b'\n', b'\r\n'), expected_tail())

    def test_checked_in_oss_tail_registers_after_signature_check(self):
        tail = (HERE / 'postflight_mozkey_oss_cleanup.sh').read_bytes()
        combined = filter_script.transform(expected_base(), tail)
        signature_check = (
            b'if ! /usr/bin/codesign --verify --deep --strict "$_mozkey_app"; then'
        )
        register_command = b'--register_input_source'
        self.assertIn(signature_check, combined)
        self.assertEqual(combined.count(register_command), 1)
        self.assertLess(combined.rfind(signature_check),
                        combined.index(register_command))
        self.assertIn(b'${COMMAND_LINE_INSTALL:-}', combined)
        self.assertIn(b'/usr/bin/sudo -u "$_mozkey_console_user"', combined)
        self.assertNotIn(filter_script.UNSAFE_LINE, combined)
        self.assertTrue(combined.endswith(b'\nexit 0\n'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
