import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from CameraReferenceSelectionEvidence import camera_reference_selection_checks


class CameraReferenceSelectionEvidenceTests(unittest.TestCase):
    @staticmethod
    def report():
        source = str(Path.cwd() / 'manual-reference.jpg')
        trace = {key: True for key in ('manual_fixture_created', 'manual_command_accepted', 'manual_loaded_unauthed',
                                       'auto_button_clicked', 'auto_restored_original', 'request_invariants',
                                       'photo_intent_history_unchanged', 'command_trace_passed')}
        trace.update(manual_source=source,
                     canonical_commands=[{'command': 'camera-reference.load', 'source': source}, {'command': 'camera-reference.auto'}],
                     original_info={'kind': 'embedded-preview', 'previewPixelSha256': 'a' * 64},
                     manual_info={'kind': 'manual-reference', 'pairStatus': 'user-selected-not-authenticated', 'previewPixelSha256': 'b' * 64},
                     automatic_info={'kind': 'embedded-preview', 'previewPixelSha256': 'a' * 64})
        return {'ui_camera_reference_selection_passed': True, 'ui_camera_reference_selection': trace}

    def test_complete_selection_and_restored_real_preview_pass(self):
        self.assertTrue(all(camera_reference_selection_checks(self.report()).values()))

    def test_forged_commands_and_preview_claims_fail(self):
        mutations = [
            lambda t: t['canonical_commands'].reverse(),
            lambda t: t['canonical_commands'][1].update(source=t['manual_source']),
            lambda t: t['canonical_commands'][0].update(source='relative.jpg'),
            lambda t: t.update(canonical_commands=[]),
            lambda t: t.update(manual_source='relative.jpg'),
            lambda t: t['manual_info'].update(pairStatus='metadata-match-not-authentication'),
            lambda t: t['manual_info'].update(previewPixelSha256='a' * 64),
            lambda t: t['automatic_info'].update(previewPixelSha256='c' * 64),
            lambda t: t['automatic_info'].update(kind='manual-reference'),
            lambda t: t['original_info'].update(previewPixelSha256='not-a-digest'),
            lambda t: t['original_info'].update(kind='manual-reference'),
        ]
        for mutate in mutations:
            report = self.report()
            mutate(report['ui_camera_reference_selection'])
            self.assertFalse(all(camera_reference_selection_checks(report).values()))

    def test_missing_and_non_boolean_acceptance_evidence_fail(self):
        original = self.report()
        for key, value in original['ui_camera_reference_selection'].items():
            if value is not True:
                continue
            for invalid in (False, 1, 'true', None):
                report = copy.deepcopy(original)
                report['ui_camera_reference_selection'][key] = invalid
                self.assertFalse(all(camera_reference_selection_checks(report).values()))
        for report in ({}, {'ui_camera_reference_selection': []}, {'ui_camera_reference_selection_passed': 1}):
            self.assertFalse(all(camera_reference_selection_checks(report).values()))


if __name__ == '__main__':
    unittest.main()
