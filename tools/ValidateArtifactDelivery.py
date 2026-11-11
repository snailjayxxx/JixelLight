"""Require complete artifact delivery without hiding original product failures."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re

OUTCOMES = {'success', 'failure', 'cancelled', 'skipped', ''}


def evaluate(platform, prior_job_status, package_gate, states):
    if platform not in ('Windows', 'macOS', 'Linux') or not isinstance(states, dict):
        raise ValueError('Invalid artifact-delivery context')
    required = ['validation'] if platform == 'Linux' else ['validation', 'package', 'shipping']
    errors, recovered = [], []
    if prior_job_status != 'success':
        errors.append('Existing product/job checks did not succeed')
    if platform != 'Linux' and package_gate != 'success':
        errors.append('Required deployed-package validation did not succeed')
    for name in required:
        pair = states.get(name, {})
        if not isinstance(pair, dict):
            errors.append(name + ': invalid attempt record'); continue
        primary, retry = (pair.get(key, {}) for key in ('primary', 'retry'))
        if not all(isinstance(value, dict) for value in (primary, retry)):
            errors.append(name + ': invalid attempt record'); continue
        a, b = primary.get('outcome', ''), retry.get('outcome', '')
        if not isinstance(a, str) or not isinstance(b, str) or a not in OUTCOMES or b not in OUTCOMES:
            errors.append(name + ': unknown attempt outcome'); continue
        chosen = primary if a == 'success' else retry if a in ('failure', 'cancelled') and b == 'success' else None
        if a == 'success' and b not in ('', 'skipped'):
            errors.append(name + ': unexpected retry after successful original upload')
        if chosen is None:
            errors.append(name + ': no completed upload'); continue
        identity, digest = chosen.get('id', ''), chosen.get('digest', '')
        if not isinstance(identity, str) or re.fullmatch(r'[1-9][0-9]*', identity) is None:
            errors.append(name + ': missing completed artifact identity')
        if not isinstance(digest, str) or re.fullmatch(r'(?:sha256:)?[0-9a-fA-F]{64}', digest) is None:
            errors.append(name + ': missing completed artifact digest')
        if chosen is retry:
            recovered.append(name)
    return {'schema': 1, 'scope': 'artifact-delivery; existing product checks remain mandatory',
            'platform': platform, 'prior_job_status': prior_job_status, 'package_gate': package_gate,
            'required': required, 'attempts': states, 'transport_recovered': recovered,
            'transport_gate_reruns_product_tests': False, 'errors': errors, 'passed': not errors}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--platform', required=True, choices=('Windows', 'macOS', 'Linux'))
    parser.add_argument('--prior-job-status', required=True)
    parser.add_argument('--package-gate', default='')
    parser.add_argument('--state-env', default='JIXELLIGHT_ARTIFACT_DELIVERY')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.report.exists() or args.report.is_symlink():
            raise ValueError('Refusing to replace an existing delivery audit')
        result = evaluate(args.platform, args.prior_job_status, args.package_gate,
                          json.loads(os.environ.get(args.state_env, '{}')))
        payload = json.dumps(result, indent=2, ensure_ascii=False) + '\n'
        with args.report.open('x', encoding='utf-8') as output:
            output.write(payload)
        print(payload, end='', flush=True)
        summary = os.environ.get('GITHUB_STEP_SUMMARY')
        if summary:
            with open(summary, 'a', encoding='utf-8') as output:
                output.write('\nArtifact-delivery audit (original outcomes retained):\n\n```json\n' + payload + '```\n')
        return 0 if result['passed'] else 1
    except (OSError, ValueError, TypeError) as error:
        print(json.dumps({'passed': False, 'scope': 'artifact-delivery', 'error': str(error)}), flush=True)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
