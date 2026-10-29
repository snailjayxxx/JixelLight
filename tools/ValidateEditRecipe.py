"""Validate the private F6 recipe clipboard GUI candidate without claiming production acceptance."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

CASES = (
    'actual-menu-opens-bounded-english-recipe',
    'readonly-preview-owns-navigation-and-canvas-keys',
    'textfield-undo-preserves-photo-history',
    'snapshot-exact-1Mi-accepted',
    'snapshot-over-1Mi-rejected-preserving-previous',
    'preview-bounds-escaped-nbsp-flood',
    'preview-truncation-keeps-surrogate-pair-intact',
    'button-snapshot-retains-unicode-escaped-preview',
    'snapshot-nontext-preserves-previous',
    'readonly-keyboard-cannot-mutate-snapshot',
    'readonly-direct-ime-cannot-mutate-snapshot',
    'readonly-direct-drop-cannot-mutate-snapshot',
    'readonly-direct-paste-cannot-mutate-snapshot',
    'invalid-nbsp-json-whitespace-rejected-atomically',
    'preview-carriage-return-visible',
    'malformed-json', 'object-instead-of-array', 'empty-array',
    'atomic-invalid-second-command', 'unknown-command-field', 'nonfinite-number',
    'oversize-text', 'too-many-commands', 'cancel-valid-recipe-is-readonly',
    'escape-valid-recipe-is-readonly', 'three-commands-apply-as-one-history-step',
    'actual-undo-restores-entire-recipe', 'actual-redo-restores-entire-recipe',
    'same-source-virtual-copy-invalidates-target',
    'chinese-current-copy-edit-preserves-original',
    'project-reopen-preserves-replayed-edits-and-undo',
    'ten-thousand-command-snapshot-exact',
    'ten-thousand-commands-apply-one-history-step',
    'ten-thousand-commands-undo-atomically',
)
CAPTURES = ('english-compact', 'truncated-preview', 'invalid-command', 'chinese-compact')
MARKERS = ('TypeError:', 'ReferenceError:', 'Unable to assign [undefined]',
           'QQmlApplicationEngine failed', 'Binding loop detected')

class InvalidEvidence(ValueError):
    pass

def require(condition, message):
    if not condition:
        raise InvalidEvidence(message)

def finite_number(value):
    require(type(value) in (int, float) and math.isfinite(value), 'non-finite number')
    return value

def check_evidence(data, native, expected_stamp, qpa, dpr, mode="clipboard"):
    """Check untrusted smoke output and associated real PNGs, not the product as a whole."""
    require(isinstance(data, dict), 'report must be an object')
    require(data['passed'] is True and data['error'] == '' and data['phase'] == 20, 'test did not finish')
    require(type(data['deadline_ms']) is int and data['deadline_ms'] == 60000, 'GUI deadline changed')
    require(0 < finite_number(data['elapsed_ms']) < 60000, 'GUI elapsed out of bounds')
    for key in ('actual_App_UI', 'QPA_controlled_focus', 'source_unchanged', 'settings_isolated',
                'owned_small_fixture_not_real_Sony'):
        require(data[key] is True, 'missing true context: ' + key)
    for key in ('production_accepted', 'full_F6_registry_or_MCP_complete', 'physical_OS_activation_verified'):
        require(data[key] is False, 'misleading acceptance: ' + key)
    require(mode in ('clipboard', 'fake'), 'unsupported mode')
    if mode == 'clipboard':
        for key in ('candidate_production_bridge_integrated','clipboard_isolated_for_test','qa_race_simulation_executed'):
            require(data[key] is True, 'missing actual clipboard context: ' + key)
        require(data['physical_input_verified'] is False, 'misleading physical input')
    else:
        require(data['candidate_production_bridge_integrated'] is False, 'fake mode must not claim the production bridge')
        require(data['qa_clipboard_fake_only'] is True and data['qa_fake_provider_for_all_snapshots'] is True,
                'fake provider evidence')
        require(data['physical_OS_clipboard_verified'] is False, 'fake mode must not claim native clipboard')
    require(type(data['source_pixels']) is int and data['source_pixels'] == 128*96, 'fixture size')
    require(isinstance(data['source_sha256'], str) and re.fullmatch(r'[0-9a-f]{64}', data['source_sha256']), 'fixture hash')
    require(data['raw_compiled_stamp'] == expected_stamp and re.fullmatch(r'[0-9a-f]{7,40}', expected_stamp), 'build stamp')
    require(data['actual_qpa'] == qpa and finite_number(data['actual_dpr']) == dpr, 'actual platform or scale')
    if mode == 'clipboard':
        require(data['offscreen_qclipboard_tested'] is (qpa == 'offscreen'), 'offscreen clipboard result')
        require(data['native_qclipboard_tested'] is (qpa != 'offscreen'), 'native clipboard result')
    rows = data['records']
    require(isinstance(rows, list) and len(rows) == len(CASES), 'case count')
    require([r['case'] for r in rows] == list(CASES), 'case names/order')
    require(all(r['passed'] is True for r in rows), 'case failure')
    for row in rows[:23]:
        require(row['dialog_visible'] is True and row['history_size'] == 2 and row['state_exposure'] == .25, 'read-only baseline')
    for row in rows[23:25]:
        require(row['dialog_visible'] is False and row['history_size'] == 2 and row['state_exposure'] == .25, 'cancel baseline')
    unicode_row=rows[7]
    require(unicode_row['snapshot'] == '雪😀\u00a0\u2028\u2029' and unicode_row['preview'] == r'雪😀\u00A0\u2028\u2029', 'unicode snapshot and preview')
    invalid=rows[13]
    require(invalid['snapshot'] == '[\u00a0{"command":"develop.set","parameter":"exposure","value":1.5}]', 'NBSP original snapshot')
    require(isinstance(invalid['error_text'], str) and invalid['error_text'].startswith('Cannot read the JSON recipe: '), 'NBSP invalid error')
    carriage=rows[14]
    require(carriage['snapshot'] == '[\r{"command":"look.reset"}]', 'carriage-return original snapshot')
    require(carriage['preview'] == r'[\u000D{"command":"look.reset"}]' and carriage['expected_preview'] == carriage['preview'], 'carriage-return preview')
    for index, exposure in ((25,.75), (26,.25), (27,.75), (32,1.75), (33,.75)):
        require(rows[index]['dialog_visible'] is False and rows[index]['state_exposure'] == exposure, 'apply or undo exposure')
    require(rows[25]['history_size'] == 3 and rows[32]['history_size'] == 4 and rows[33]['history_size'] == 4, 'single history steps')
    require(rows[28]['dialog_visible'] is True and rows[29]['dialog_visible'] is False, 'target selection')
    require(rows[31]['commands'] == 10000 and 0 < rows[31]['utf16_units'] <= 1048576, 'maximum command batch')
    keys=data['key_events']
    require(isinstance(keys, list) and len(keys) >= 15 and all(type(k['key']) is int and type(k['modifiers']) is int and type(k['shortcut_consumed']) is bool for k in keys), 'injected key evidence')
    captures=data['captures']
    require(isinstance(captures, list) and len(captures) == len(CAPTURES), 'capture count')
    require([c['label'] for c in captures] == list(CAPTURES), 'capture labels')
    native_path = Path(native)
    require(native_path.is_file() and not native_path.is_symlink(), 'native report file')
    native = native_path.resolve()
    for capture in captures:
        require(finite_number(capture['actual_dpr']) == dpr, 'capture scale')
        raw_path=Path(capture['path'])
        require(not raw_path.is_symlink(), 'capture is a symbolic link')
        path=raw_path.resolve()
        require(path == Path(str(native)+'.'+capture['label']+'.png').resolve(), 'capture path')
        require(path.is_file(), 'capture file')
        blob=path.read_bytes()
        require(hashlib.sha256(blob).hexdigest() == capture['sha256'], 'capture SHA')
        require(len(blob)>=24 and blob[:8] == b'\x89PNG\r\n\x1a\n' and blob[12:16] == b'IHDR', 'capture PNG')
        width,height=struct.unpack('>II',blob[16:24])
        require((width,height) == (capture['width'],capture['height']), 'capture dimensions')
        require(width == int(1180*dpr) and height == int(720*dpr), 'compact window dimensions')
    return {'cases':len(rows),'captures':len(captures),'elapsed_ms':data['elapsed_ms'],
            'actual_qpa':qpa,'actual_dpr':dpr,'native_qclipboard_tested':data.get('native_qclipboard_tested',False),
            'fake_clipboard_only':mode == 'fake','production_accepted':False}

def strict_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'duplicate JSON key: ' + key)
            result[key] = value
        return result
    def reject_constant(value):
        raise InvalidEvidence('nonfinite JSON value: ' + value)
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique, parse_constant=reject_constant)

def run(binary, output_dir, stamp, qpa, dpr, isolated_ci_native=False, mode='clipboard', environment=None):
    base_env = os.environ if environment is None else environment
    require(mode in ('clipboard','fake'), 'unsupported mode')
    # Only the clipboard mode writes QClipboard; refuse native access outside isolated GitHub CI.
    if mode == 'clipboard' and qpa != 'offscreen' and (not isolated_ci_native or base_env.get('GITHUB_ACTIONS') != 'true'):
        raise InvalidEvidence('Native clipboard testing requires an explicitly isolated GitHub Actions runner')
    require(dpr in (1,2), 'unsupported device scale')
    binary=Path(binary).resolve(strict=True)
    output=Path(output_dir)
    output.mkdir(parents=True, exist_ok=False)
    output=output.resolve()
    native=output/'recipe.native.json'; logfile=output/'recipe.native.log'; resultfile=output/'recipe.validation.json'
    env=base_env.copy()
    for key in ('JIXELLIGHT_REQUIRE_GPU','QSG_RHI_BACKEND','QSG_RHI_PREFER_SOFTWARE_RENDERER',
                'JIXELLIGHT_CI_ISOLATED_CLIPBOARD','QT_QPA_OFFSCREEN_NO_GLX'):
        env.pop(key,None)
    env.update(QT_QPA_PLATFORM=qpa,QT_SCALE_FACTOR=str(dpr),QT_QUICK_BACKEND='software',
               JIXELLIGHT_FORCE_CPU='1',JIXELLIGHT_CPU_THREADS='2')
    if qpa=='offscreen': env['QT_QPA_OFFSCREEN_NO_GLX']='1'
    elif mode == 'clipboard': env['JIXELLIGHT_CI_ISOLATED_CLIPBOARD']='1'
    with tempfile.TemporaryDirectory(prefix='jixellight-recipe-') as temp:
        for key in ('USERPROFILE','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','APPDATA','LOCALAPPDATA'):
            env[key]=temp
        try:
            option='--edit-recipe-clipboard-smoke-report' if mode == 'clipboard' else '--edit-recipe-smoke-report'
            completed=subprocess.run([str(binary),option,str(native)],
                cwd=temp,env=env,capture_output=True,text=True,errors='replace',timeout=75,check=False)
            code,log=completed.returncode,completed.stdout+'\n'+completed.stderr
        except (OSError,subprocess.TimeoutExpired) as error:
            code,log=-1,str(error)
    with logfile.open('x',encoding='utf-8') as f: f.write(log)
    result={'mode':'edit-recipe-'+mode,'passed':False,'returncode':code,'production_accepted':False,'native_report':str(native),
            'limitations':['controlled QPA events are not physical input','128x96 generated fixture is not real Sony',
                'a successful private test is not full F6 acceptance']}
    try:
        require(code==0, 'GUI process failed')
        require(not any(marker in log for marker in MARKERS), 'QML runtime error')
        require(native.is_file() and not native.is_symlink(), 'native report file')
        data=strict_json(native)
        result['evidence']=check_evidence(data,native,stamp,qpa,dpr,mode)
        result['passed']=True
    except (InvalidEvidence,KeyError,TypeError,ValueError,OSError,OverflowError,struct.error) as error:
        result['error']=str(error)
    with resultfile.open('x',encoding='utf-8') as f: json.dump(result,f,ensure_ascii=False,indent=2)
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary');parser.add_argument('output_dir');parser.add_argument('--expected-stamp',required=True)
    parser.add_argument('--qpa',choices=('offscreen','cocoa','windows','xcb'),default='offscreen')
    parser.add_argument('--dpr',type=int,choices=(1,2),default=1)
    parser.add_argument('--isolated-ci-native',action='store_true')
    parser.add_argument('--mode',choices=('clipboard','fake'),default='clipboard')
    args=parser.parse_args()
    try:
        result=run(args.binary,args.output_dir,args.expected_stamp,args.qpa,args.dpr,args.isolated_ci_native,args.mode)
    except (InvalidEvidence,FileExistsError,FileNotFoundError,OSError) as error:
        print(json.dumps({'passed':False,'error':str(error),'production_accepted':False}),file=sys.stderr)
        return 1
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return 0 if result['passed'] else 1

if __name__=='__main__': raise SystemExit(main())
