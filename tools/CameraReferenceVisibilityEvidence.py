"""Independent checks for the ordinary RAW App's reference visibility commands."""


def camera_reference_visibility_checks(report):
    trace = report.get('ui_camera_reference_visibility')
    if not isinstance(trace, dict):
        trace = {}
    commands = trace.get('canonical_commands')
    exact_commands = (isinstance(commands, list) and len(commands) == 2
                      and all(isinstance(command, dict)
                              and set(command) == {'command', 'enabled'}
                              and command['command'] == 'camera-reference.show'
                              and command['enabled'] is enabled
                              for command, enabled in zip(commands, (False, True))))
    return {
        'reported_command_trace': report.get('ui_camera_reference_command_trace_passed') is True,
        'legacy_adapter_hides': trace.get('adapter_hidden') is True,
        'canonical_show_accepted': trace.get('command_accepted') is True,
        'intent_history_revision_source_unchanged': trace.get('intent_history_revision_source_unchanged') is True,
        'trace_boundary_and_order': trace.get('command_trace_passed') is True,
        'two_exact_boolean_commands': exact_commands,
        'reference_finishes_loading': trace.get('reference_loaded') is True,
    }
