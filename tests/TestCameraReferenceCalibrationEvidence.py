import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from CameraReferenceCalibrationEvidence import camera_reference_calibration_checks


class CameraReferenceCalibrationEvidenceTests(unittest.TestCase):
    @staticmethod
    def report():
        commands = [{'command': 'camera-reference.fit'}, {'command': 'camera-reference.fit.cancel'}]
        trace = {key: True for key in ('reference_ready', 'legacy_fit_adapter_accepted', 'start_notification_observed',
                                       'start_trace_before_cancel', 'canonical_cancel_accepted', 'same_call_cancellation_settled',
                                       'command_trace_passed', 'intent_history_revision_source_unchanged')}
        trace.update(before_state={'exposure': .4}, after_state={'exposure': .4}, before_cursor=3, after_cursor=3,
                     before_revision='12', after_revision='12', before_source_cache_key='4294967297',
                     after_source_cache_key='4294967297', canonical_commands=commands,
                     accepted_events=[dict(command=commands[0], revision='12', photo_epoch='2',
                                           completion='calibration-request-accepted', source_identity='fixture-identity'),
                                      dict(command=commands[1], revision='12', photo_epoch='2',
                                           completion='calibration-cancel-request-accepted', was_busy=True)])
        return {'look_validation_required': True, 'ui_camera_reference_calibration_passed': True,
                'ui_camera_reference_calibration': trace}

    def test_complete_cancellation_and_frozen_owner_pass(self):
        self.assertTrue(all(camera_reference_calibration_checks(self.report()).values()))

    def test_forged_order_ownership_and_state_changes_fail(self):
        mutations = [lambda t: t['canonical_commands'].reverse(),
                     lambda t: t['canonical_commands'][0].update(extra=True),
                     lambda t: t['accepted_events'][1].update(photo_epoch='3'),
                     lambda t: t['accepted_events'][0].update(revision='13'),
                     lambda t: t['accepted_events'][0].update(source_identity=''),
                     lambda t: t['accepted_events'][1].update(was_busy=1),
                     lambda t: t.update(accepted_events=[]),
                     lambda t: t.update(after_state={'exposure': .5}),
                     lambda t: t.update(after_cursor=4),
                     lambda t: t.update(before_cursor=True, after_cursor=True),
                     lambda t: t.update(after_revision='13'),
                     lambda t: t.update(after_source_cache_key='4294967298'),
                     lambda t: t.update(before_revision=12, after_revision=12)]
        for mutate in mutations:
            report = self.report()
            mutate(report['ui_camera_reference_calibration'])
            self.assertFalse(all(camera_reference_calibration_checks(report).values()))

    def test_missing_or_non_boolean_acceptance_fails(self):
        original = self.report()
        for key, value in original['ui_camera_reference_calibration'].items():
            if value is not True:
                continue
            for invalid in (False, 1, 'true', None):
                report = copy.deepcopy(original)
                report['ui_camera_reference_calibration'][key] = invalid
                self.assertFalse(all(camera_reference_calibration_checks(report).values()))
        for report in ({}, {'ui_camera_reference_calibration': []}, {'look_validation_required': 1}):
            self.assertFalse(all(camera_reference_calibration_checks(report).values()))


if __name__ == '__main__':
    unittest.main()
