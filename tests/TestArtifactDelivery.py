"""Behavioral checks for the mandatory post-upload gate; no live uploads."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from ValidateArtifactDelivery import evaluate


def state(platform='Windows'):
    names = ['validation'] if platform == 'Linux' else ['validation', 'package', 'shipping']
    return {name: {'primary': {'outcome': 'success', 'id': str(i+1), 'digest': 'a'*64},
                   'retry': {'outcome': 'skipped', 'id': '', 'digest': ''}} for i, name in enumerate(names)}


class ArtifactDeliveryTests(unittest.TestCase):
    def test_all_platforms_require_complete_original_deliveries(self):
        for platform in ('Windows', 'macOS', 'Linux'):
            result = evaluate(platform, 'success', 'success', state(platform))
            self.assertTrue(result['passed']); self.assertEqual(result['transport_recovered'], [])

    def test_transport_recovery_retains_original_failed_outcomes(self):
        for original in ('failure', 'cancelled'):
            data = state(); data['validation']['primary']['outcome'] = original
            data['validation']['retry'] = {'outcome': 'success', 'id': '40', 'digest': 'sha256:'+'b'*64}
            result = evaluate('Windows', 'success', 'success', data)
            self.assertTrue(result['passed']); self.assertEqual(result['transport_recovered'], ['validation'])
            self.assertEqual(result['attempts']['validation']['primary']['outcome'], original)
            self.assertFalse(result['transport_gate_reruns_product_tests'])

    def test_successful_uploads_cannot_hide_failed_or_cancelled_product_checks(self):
        for platform in ('Windows', 'macOS', 'Linux'):
            for outcome in ('failure', 'cancelled', 'skipped', ''):
                self.assertFalse(evaluate(platform, outcome, 'success', state(platform))['passed'])
        for outcome in ('failure', 'cancelled', 'skipped', ''):
            self.assertFalse(evaluate('Windows', 'success', outcome, state())['passed'])

    def test_every_missing_or_failed_required_upload_refuses_success(self):
        for name in ('validation', 'package', 'shipping'):
            for outcome in ('failure', 'cancelled', 'skipped', ''):
                data = state(); data[name]['primary']['outcome'] = outcome
                self.assertFalse(evaluate('Windows', 'success', 'success', data)['passed'])
            data = state(); del data[name]
            self.assertFalse(evaluate('Windows', 'success', 'success', data)['passed'])

    def test_completed_identity_and_digest_are_required(self):
        for key, value in (('id', ''), ('id', '0'), ('id', '1e2'), ('id', 12), ('digest', ''), ('digest', 'a'*63)):
            data = state(); data['package']['primary'][key] = value
            self.assertFalse(evaluate('Windows', 'success', 'success', data)['passed'])

    def test_unexpected_retries_and_unknown_outcomes_refuse_success(self):
        data = state(); data['validation']['retry'] = {'outcome': 'success', 'id': '5', 'digest': 'b'*64}
        self.assertFalse(evaluate('Windows', 'success', 'success', data)['passed'])
        data = state(); data['validation']['primary']['outcome'] = 'unknown'
        self.assertFalse(evaluate('Windows', 'success', 'success', data)['passed'])
        with self.assertRaises(ValueError): evaluate('offscreen', 'success', 'success', {})

    def test_cli_keeps_failed_audit_and_refuses_existing_report(self):
        with tempfile.TemporaryDirectory() as temp:
            report = Path(temp)/'audit.json'; env = os.environ.copy(); data = state('Linux')
            data['validation']['primary']['outcome'] = 'failure'; env['JIXELLIGHT_ARTIFACT_DELIVERY'] = json.dumps(data)
            env.pop('GITHUB_STEP_SUMMARY', None)
            script = Path(__file__).resolve().parents[1]/'tools/ValidateArtifactDelivery.py'
            args = [sys.executable, str(script), '--platform', 'Linux', '--prior-job-status', 'success', '--report', str(report)]
            run = subprocess.run(args, env=env, capture_output=True, text=True)
            self.assertEqual(run.returncode, 1); self.assertFalse(json.loads(report.read_text())['passed'])
            before = report.read_bytes(); again = subprocess.run(args, env=env, capture_output=True, text=True)
            self.assertEqual(again.returncode, 2); self.assertEqual(report.read_bytes(), before)


if __name__ == '__main__': unittest.main()
