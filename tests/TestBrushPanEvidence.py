"""Pan evidence requires the submitted final viewport and a changed actual ROI."""
from copy import deepcopy
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ValidateBrushCanvas import _pan_checks


def fixture(report):
    guards=['modifiers-ignored','orphan-repeat-ignored','repeat-release-retains-navigation','radius-held-during-navigation',
        'escape-keeps-brush-mode','textfield-owns-space-focus-loss','mouse-held-and-cancelled-until-lift','pen-held-and-cancelled-primary-lift',
        'pen-during-pan-ignored-until-lift-fresh-press','window-deactivation-cancels-held-pan','mode-exit-cancels-space',
        'crop-entry-cancels-space','fit-ignores-space','source-switch-cancels-held-pan-original-preserved']
    d=dict(pan_protocol_version=2,pan_checked=True,pan_readonly=True,pan_mapping=True,pan_guards_checked=True,pan_geometry_checked=True,
        pan_started_brush=False,physical_OS_activation_verified=False,window_focus_injection='Qt-QPA-controlled-window-focus',
        pan_guard_records=[dict(case=g,passed=True,document_history_unchanged=True)for g in guards],logical_window_size=[1180,720],
        pan_window_check=dict(active_before=True,active_after_reactivation=True,stable=True,active_after_deactivation=False,navigation_after_deactivation=False))
    d['pan_record']=dict(dpr=2,source_width=2203,source_height=1207,zoom=2,space_released_before_mouse_lift=True,
        document_history_unchanged=True,view_mapping_exact=True,actual_center=[.5-96/(2203*2),.5+48/(1207*2)])
    d['pan_view_records']=[]
    for i,(case,size,zoom,delta)in enumerate(zip(['100-percent','rotated-cropped','plain-rotated-cropped'],[[2203,1207],[846,1653],[846,1653]],[1,2,2],[(12,-8),(26,-16),(18,-10)])):
        center=[.5-delta[k]*2/(size[k]*zoom)for k in (0,1)]
        d['pan_view_records'].append(dict(case=case,output_size=size,zoom=zoom,dpr=2,center_before=[.5,.5],frame_before=[.35,.35,.3,.3],
            frame_after=[center[0]-.15,center[1]-.15,.3,.3],actual_center=center,expected_center=center[:],mapping_exact=True,document_history_unchanged=True,
            release_viewport_submitted=True,revision_before_input=str(10+2*i),revision_after_release=str(11+2*i),pointer_owned_closed_hand=True,
            mask_tool='brush'if i<2 else'',hint_visible_bounded=i<2,hint_policy_correct=True))
    d['pan_hint_records']=[dict(language=lang,text=text,visible_bounded=True,width=150,canvas_width=400)for lang,text in [('en_US','Space + drag to pan'),('zh_CN','空格＋拖动平移')]]
    # This parser reads PNG dimension headers; complete pixel validation belongs
    # to the unchanged native Canvas workflow, not these protocol unit fixtures.
    for suffix in ['.pan.png','.pan-100-percent.png','.pan-rotated-cropped.png','.pan-plain-rotated-cropped.png','.pan-hint-en_US.png','.pan-hint-zh_CN.png']:
        Path(str(report)+suffix).write_bytes(b'\x89PNG\r\n\x1a\n'+struct.pack('>I',13)+b'IHDR'+struct.pack('>II',2360,1440))
    return d


class BrushPanEvidenceTests(unittest.TestCase):
    def check_changes(self,changes):
        with tempfile.TemporaryDirectory()as temp:
            report=Path(temp)/'native.json';base=fixture(report)
            for change in changes:
                data=deepcopy(base);change(data)
                self.assertFalse(all(_pan_checks(data,report).values()))

    def test_complete_v2_preserves_all_original_pan_checks(self):
        with tempfile.TemporaryDirectory()as temp:
            report=Path(temp)/'native.json';checks=_pan_checks(fixture(report),report)
            self.assertEqual(len(checks),10);self.assertTrue(all(checks.values()))

    def test_old_protocol_and_fabricated_submission_flags_are_refused(self):
        self.check_changes([lambda d:d.update(pan_protocol_version=1)]+[
            lambda d,v=v:d['pan_view_records'][2].update(release_viewport_submitted=v)for v in (False,1,None,'true')])

    def test_noncanonical_stale_or_missing_release_revisions_are_refused(self):
        changes=[lambda d,k=k,v=v:d['pan_view_records'][2].update({k:v})for k in ('revision_before_input','revision_after_release')for v in ('0','01','+1','18446744073709551616',True,12,None)]
        changes += [lambda d:d['pan_view_records'][2].update(revision_after_release='14'),lambda d:d['pan_view_records'][2].pop('revision_before_input')]
        self.check_changes(changes)

    def test_matching_centers_cannot_hide_an_unchanged_or_invalid_actual_frame(self):
        self.check_changes([lambda d:d['pan_view_records'][2].update(frame_after=d['pan_view_records'][2]['frame_before'][:]),
            lambda d:d['pan_view_records'][2].update(frame_after=[.9,.9,.3,.3]),lambda d:d['pan_view_records'][2].pop('frame_after')])

    def test_original_center_precision_and_frame_dimensions_stay_strict(self):
        self.check_changes([lambda d:d['pan_view_records'][2].update(actual_center=[.6,.6]),
            lambda d:d['pan_view_records'][2].update(frame_after=[.2,.2,.4,.3])])

    def test_original_guards_pointer_and_hint_policies_remain_required(self):
        self.check_changes([lambda d:d['pan_guard_records'].pop(),lambda d:d['pan_view_records'][2].update(pointer_owned_closed_hand=False),
            lambda d:d['pan_view_records'][2].update(hint_policy_correct=False)])


if __name__=='__main__':unittest.main()
