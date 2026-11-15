"""Independent evidence for accepted reference-fit and same-call cancellation."""


def camera_reference_calibration_checks(report):
    trace = report.get('ui_camera_reference_calibration')
    if not isinstance(trace, dict):
        trace = {}
    expected = [{'command': 'camera-reference.fit'}, {'command': 'camera-reference.fit.cancel'}]
    commands = trace.get('canonical_commands')
    events = trace.get('accepted_events')

    def decimal(value):
        return isinstance(value, str) and 0 < len(value) <= 20 and value.isascii() and value.isdecimal()

    revision = trace.get('before_revision')
    source = trace.get('before_source_cache_key')
    owner_events = (isinstance(events, list) and len(events) == 2 and all(isinstance(e, dict) for e in events))
    if owner_events:
        owner_events = (decimal(revision) and decimal(events[0].get('photo_epoch'))
                        and events[0].get('photo_epoch') == events[1].get('photo_epoch')
                        and all(e.get('revision') == revision for e in events)
                        and [e.get('command') for e in events] == expected
                        and events[0].get('completion') == 'calibration-request-accepted'
                        and events[1].get('completion') == 'calibration-cancel-request-accepted'
                        and isinstance(events[0].get('source_identity'), str) and bool(events[0]['source_identity'])
                        and events[1].get('was_busy') is True)
    before, after = trace.get('before_state'), trace.get('after_state')
    cursor = trace.get('before_cursor')
    return {
        'reported_calibration_passed': report.get('ui_camera_reference_calibration_passed') is True,
        'real_RAW_reference_ready': report.get('look_validation_required') is True and trace.get('reference_ready') is True,
        'legacy_fit_adapter_accepted': trace.get('legacy_fit_adapter_accepted') is True,
        'start_notification_and_pre_callback_trace': trace.get('start_notification_observed') is True
            and trace.get('start_trace_before_cancel') is True,
        'canonical_current_fit_cancel_accepted': trace.get('canonical_cancel_accepted') is True,
        'same_call_cancellation_settled': trace.get('same_call_cancellation_settled') is True,
        'intent_and_history_unchanged': trace.get('intent_history_revision_source_unchanged') is True
            and isinstance(before, dict) and bool(before) and before == after
            and type(cursor) is int and cursor >= 0 and type(trace.get('after_cursor')) is int
            and cursor == trace['after_cursor'],
        'revision_and_source_unchanged': decimal(revision) and revision == trace.get('after_revision')
            and decimal(source) and source == trace.get('after_source_cache_key'),
        'two_exact_fit_cancel_commands': commands == expected,
        'frozen_acceptance_owner_and_order': trace.get('command_trace_passed') is True and bool(owner_events),
    }
