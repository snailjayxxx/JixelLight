"""Check the real shortcut guide, modal key routing and held-input ownership."""
from __future__ import annotations
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
    'empty-library-guide-and-keys-read-only', 'english-compact-native-key-layout',
    'guide-owns-navigation-edit-and-canvas-keys', 'tabbed-close-escape-dismisses-guide',
    'open-guide-cancels-pan-retains-brush', 'content-escape-retains-brush',
    'closing-guide-restores-brush-key-focus', 'help-return-escape-preserve-crop',
    'canvas-escape-still-cancels-crop', 'chinese-compact-native-key-layout',
    'open-guide-resize-stays-centered', 'keyboard-open-guide-cancels-held-pressure',
    'modal-close-keeps-cancelled-owner-until-primary-lift',
    'fresh-pressure-after-primary-lift-cancels-without-edit',
)
MARKERS = ('TypeError:', 'ReferenceError:', 'Unable to assign [undefined]',
           'QQmlApplicationEngine failed', 'Binding loop detected')


def _number(value):
    assert type(value) in (int, float) and math.isfinite(value)
    return value


def _checks(data: dict, native: Path) -> dict[str, bool]:
    checks = dict.fromkeys(('protocol', 'readonly_contexts', 'native_labels',
                           'compact_layout', 'modal_keys', 'held_pressure', 'captures'), False)
    try:
        assert type(data['protocol_version']) is int and data['protocol_version'] == 2
        assert data['passed'] is True and data['error'] == ''
        assert data['deadline_ms'] == 60000 and 0 < _number(data['elapsed_ms']) < 60000
        assert data['test_fixture'] is True and data['actual_photo_app_UI'] is True
        assert data['settings_isolated'] is True and data['source_kind'] == 'owned-rgba64-png'
        assert data['source_pixels'] == 1024*768 and re.fullmatch('[0-9a-f]{64}', data['source_sha256'])
        assert re.fullmatch('[0-9a-f]{7,40}', data['source_commit'])
        assert all(data[k] is False for k in ('product_UI_acceptance', 'physical_OS_activation_verified',
            'physical_keyboard_driver_verified', 'physical_tablet_verified', 'native_gpu_brush',
            'native_float_raw_decoder', 'gpu_active'))
        assert data['keyboard_injection'] == 'Qt-QPA-shortcut-override-map-then-key-events'
        assert data['pressure_injection'] == 'Qt-QPA-controlled-stylus'
        assert data['window_focus_injection'] == 'Qt-QPA-controlled-window-focus'
        assert data['crop_context_setup_internal_beginCrop'] is True and data['help_button_focus_setup_internal'] is True
        checks['protocol'] = True
        rows = data['records']
        assert len(rows) == len(CASES) and [r['case'] for r in rows] == list(CASES)
        assert data['source_unchanged'] is True and data['document_history_unchanged'] is True
        assert all(r['passed'] is True and r['document_history_source_unchanged'] is True for r in rows)
        assert rows[0]['workspace'] == 0 and all(r['workspace'] == 1 for r in rows[1:])
        assert all(rows[0][key] is True for key in ('library_workspace_zero_checked', 'library_workspace_visible', 'library_grid_visible'))
        assert type(rows[0]['library_grid_count']) is int and rows[0]['library_grid_count'] == 0
        assert rows[7]['crop_editing'] is True and rows[8]['crop_editing'] is False
        assert rows[5]['mask_tool'] == rows[6]['mask_tool'] == 'brush'
        assert rows[4]['navigation_active'] is False and rows[6]['navigation_active'] is True
        checks['readonly_contexts'] = True
        for index, language in ((0, 'en_US'), (1, 'en_US'), (9, 'zh_CN'), (10, 'zh_CN')):
            row = rows[index]
            assert row['language'] == language
            expected = ('页面与导入', '编辑历史', '笔刷画布', '裁剪画布') if language == 'zh_CN' else (
                'Workspace and import', 'Edit history', 'Brush canvas', 'Crop canvas')
            assert all(label in row['labels'] for label in expected)
            assert len(row['native_shortcuts']) == 3 and all(
                isinstance(label, str) and label and label in row['labels'] for label in row['native_shortcuts'])
        checks['native_labels'] = True
        for index in (0, 1, 9, 10):
            row = rows[index]
            width, height = _number(row['window_width']), _number(row['window_height'])
            assert width >= 1180 and height >= 720
            x, y, dw, dh = [_number(row[k]) for k in ('dialog_x', 'dialog_y', 'dialog_width', 'dialog_height')]
            assert 0 <= x and 0 <= y and x+dw <= width+1 and y+dh <= height+1
            assert abs(x+dw/2-width/2) < 1 and abs(y+dh/2-height/2) < 1
            assert abs(dw-min(width-32, 680)) < 1 and abs(dh-min(height-40, 620)) < 1
            assert row['label_bounds'] and all(_number(r['width']) > 0 and _number(r['height']) > 0
                and _number(r['painted_width']) <= r['width']+1 for r in row['label_bounds'])
            assert row['header_controls'] and all(r['contained'] is True for r in row['header_controls'])
            assert any(r['name'] == 'keyboardShortcutsButton' for r in row['header_controls'])
        checks['compact_layout'] = True
        packets = data['keyboard_packets']
        assert len(packets) >= 25 and all(type(p['key']) is int and type(p['modifiers']) is int for p in packets)
        modal = [p for p in packets if p['phase'] in (-1, 3, 4, 7, 11, 18)]
        assert modal and all(p['shortcut_consumed'] is False and p['key_press_delivered'] is True for p in modal)
        assert {p['phase'] for p in modal} == {-1, 3, 4, 7, 11, 18}
        checks['modal_keys'] = True
        assert rows[11]['pressure_owner_held'] is True and rows[11]['brush_drawing'] is False
        assert rows[11]['help_button_focus_setup_internal'] is True
        assert rows[11]['dialog_visible'] is True and rows[12]['dialog_visible'] is False
        checks['held_pressure'] = True
        captures = data['captures']
        assert len(captures) == 5 and [c['label'] for c in captures] == [
            'empty-library', 'english-compact', 'chinese-compact', 'resized', 'final']
        dpr = _number(data['dpr']); assert dpr > 0
        for capture in captures:
            assert _number(capture['dpr']) == dpr
            path = Path(capture['path']).resolve()
            assert path == Path(str(native)+'.'+capture['label']+'.png').resolve()
            blob = path.read_bytes()
            assert hashlib.sha256(blob).hexdigest() == capture['sha256']
            assert blob[:8] == b'\x89PNG\r\n\x1a\n' and blob[12:16] == b'IHDR'
            actual = struct.unpack('>II', blob[16:24])
            assert actual == (capture['width'], capture['height'])
            width, height = _number(capture['logical_width']), _number(capture['logical_height'])
            assert actual[0] == math.floor(width*dpr+.5)
            assert math.floor(720*dpr+.5) <= actual[1] <= math.floor(height*dpr+.5)
        checks['captures'] = True
    except (AssertionError, KeyError, TypeError, ValueError, OSError, OverflowError, struct.error):
        pass
    return checks


def validate_shortcut_help(binary: Path, environment: dict[str, str], sandbox_dir: Path, report: Path) -> dict:
    binary, report = binary.resolve(), report.resolve()
    native = report.with_name(report.stem+'.native.json')
    if report.exists() or native.exists():
        return {'mode': 'shortcut-help', 'passed': False, 'error': 'Refusing to overwrite existing validation evidence'}
    sandbox_dir.mkdir(parents=True, exist_ok=True); report.parent.mkdir(parents=True, exist_ok=True)
    env = environment.copy()
    for name in ('USERPROFILE', 'XDG_CACHE_HOME', 'XDG_CONFIG_HOME', 'XDG_DATA_HOME', 'APPDATA', 'LOCALAPPDATA'):
        env[name] = str(sandbox_dir)
    for name in ('JIXELLIGHT_REQUIRE_GPU', 'QSG_RHI_BACKEND', 'QSG_RHI_PREFER_SOFTWARE_RENDERER'):
        env.pop(name, None)
    env['JIXELLIGHT_FORCE_CPU'] = '1'
    if sys.platform.startswith('linux'):
        env['QT_QPA_PLATFORM'] = 'offscreen'
    try:
        run = subprocess.run([str(binary), '--shortcut-help-smoke-report', str(native)], cwd=sandbox_dir,
            env=env, capture_output=True, text=True, errors='replace', timeout=75, check=False)
        code, log = run.returncode, run.stdout+'\n'+run.stderr
    except (OSError, subprocess.TimeoutExpired) as error:
        code, log = -1, str(error)
    native.with_suffix('.log').write_text(log, encoding='utf-8')
    try:
        data = json.loads(native.read_text(encoding='utf-8'))
    except (ValueError, OSError):
        data = {}
    errors = [line for line in log.splitlines() if any(marker in line for marker in MARKERS)]
    checks = _checks(data, native)
    result = {'mode': 'shortcut-help', 'passed': code == 0 and not errors and all(checks.values()),
        'returncode': code, 'source_commit': data.get('source_commit'), 'test_fixture': True,
        'physical_tablet_verified': False, 'physical_keyboard_driver_verified': False,
        'native_gpu_brush': False, 'native_float_raw_decoder': False,
        'qml_errors': errors, 'checks': checks, 'report': data}
    report.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    if not result['passed']:
        print(log[-16000:], file=sys.stderr, flush=True)
    return result


def main() -> int:
    if len(sys.argv) != 3:
        return 2
    with tempfile.TemporaryDirectory(prefix='jixellight-shortcut-help-') as temp:
        result = validate_shortcut_help(Path(sys.argv[1]), os.environ.copy(), Path(temp), Path(sys.argv[2]))
    print(json.dumps(result, ensure_ascii=False, indent=2), flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
