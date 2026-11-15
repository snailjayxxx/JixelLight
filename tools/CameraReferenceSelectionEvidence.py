"""Independent checks for manual JPEG selection and restored real RAW previews."""
import os
import re


def camera_reference_selection_checks(report):
    trace = report.get('ui_camera_reference_selection')
    if not isinstance(trace, dict):
        trace = {}
    source = trace.get('manual_source')
    source_valid = isinstance(source, str) and 0 < len(source) <= 32768 and '\0' not in source and os.path.isabs(source)
    commands = trace.get('canonical_commands')
    exact = (source_valid and isinstance(commands, list) and len(commands) == 2
             and commands == [{'command': 'camera-reference.load', 'source': source},
                              {'command': 'camera-reference.auto'}])
    infos = []
    for key in ('original_info', 'manual_info', 'automatic_info'):
        value = trace.get(key)
        infos.append(value if isinstance(value, dict) else {})
    original, manual, automatic = infos
    digests_valid = all(isinstance(info.get('previewPixelSha256'), str)
                        and re.fullmatch(r'[0-9a-f]{64}', info['previewPixelSha256']) for info in infos)
    restored = (digests_valid and original.get('kind') in ('paired-jpeg', 'embedded-preview')
                and automatic.get('kind') == original.get('kind')
                and automatic.get('previewPixelSha256') == original.get('previewPixelSha256')
                and manual.get('previewPixelSha256') != original.get('previewPixelSha256'))
    return {
        'reported_selection_passed': report.get('ui_camera_reference_selection_passed') is True,
        'manual_fixture_created': trace.get('manual_fixture_created') is True,
        'manual_load_accepted': trace.get('manual_command_accepted') is True,
        'manual_preview_unauthed': trace.get('manual_loaded_unauthed') is True
            and manual.get('kind') == 'manual-reference' and manual.get('pairStatus') == 'user-selected-not-authenticated',
        'actual_automatic_button_clicked': trace.get('auto_button_clicked') is True,
        'real_RAW_reference_restored': trace.get('auto_restored_original') is True and restored,
        'immediate_request_intent_history_revision_source_unchanged': trace.get('request_invariants') is True,
        'complete_selection_intent_history_unchanged': trace.get('photo_intent_history_unchanged') is True,
        'trace_boundary_and_order': trace.get('command_trace_passed') is True,
        'two_exact_load_auto_commands': exact,
    }
