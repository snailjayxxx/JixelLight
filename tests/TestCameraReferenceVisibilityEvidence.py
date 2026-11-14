import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from CameraReferenceVisibilityEvidence import camera_reference_visibility_checks


class CameraReferenceVisibilityEvidenceTests(unittest.TestCase):
    @staticmethod
    def report():
        return {'ui_camera_reference_command_trace_passed': True,
                'ui_camera_reference_visibility': {
                    'adapter_hidden': True, 'command_accepted': True,
                    'intent_history_revision_source_unchanged': True,
                    'command_trace_passed': True, 'reference_loaded': True,
                    'canonical_commands': [
                        {'command': 'camera-reference.show', 'enabled': False},
                        {'command': 'camera-reference.show', 'enabled': True}]}}

    def test_complete_native_command_evidence_passes(self):
        self.assertTrue(all(camera_reference_visibility_checks(self.report()).values()))

    def test_claimed_success_cannot_replace_exact_order_types_or_fields(self):
        invalid = [[], [{'command': 'camera-reference.show', 'enabled': True}],
                   [{'command': 'camera-reference.show', 'enabled': True},
                    {'command': 'camera-reference.show', 'enabled': False}],
                   [{'command': 'camera-reference.show', 'enabled': 0},
                    {'command': 'camera-reference.show', 'enabled': 1}],
                   [False, True], 'commands', None]
        extra = self.report()['ui_camera_reference_visibility']['canonical_commands']
        extra[0]['source'] = 'injected.jpg'
        invalid.append(extra)
        for commands in invalid:
            with self.subTest(commands=commands):
                report = self.report()
                report['ui_camera_reference_visibility']['canonical_commands'] = commands
                self.assertFalse(all(camera_reference_visibility_checks(report).values()))

    def test_missing_or_non_boolean_invariants_fail(self):
        original = self.report()
        for key in ('adapter_hidden', 'command_accepted', 'intent_history_revision_source_unchanged',
                    'command_trace_passed', 'reference_loaded'):
            for value in (False, 1, 'true', None):
                report = copy.deepcopy(original)
                report['ui_camera_reference_visibility'][key] = value
                self.assertFalse(all(camera_reference_visibility_checks(report).values()))
        for report in ({}, {'ui_camera_reference_visibility': []},
                       {'ui_camera_reference_command_trace_passed': True}):
            self.assertFalse(all(camera_reference_visibility_checks(report).values()))


if __name__ == '__main__':
    unittest.main()
