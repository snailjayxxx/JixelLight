"""Reject stale, clipped and falsely attributed soft-proof acceptance evidence."""
from copy import deepcopy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ValidateSoftProof import validate_report,validate_diagnostic,validate_soft_proof
from ValidateColorMask import DEVELOP_FIELDS


def report_fixture(mode='gpu',qpa='cocoa'):
    commit='a'*40;profile='b'*64;pixels=307*211
    state={k:0 for k in DEVELOP_FIELDS}
    state.update(exposure=.2,geometry=dict(schema=1,x=0,y=0,width=1,height=1,quarterTurns=0,flipHorizontal=False,flipVertical=False),
                 look=dict(schema=1,code='',mode='off',parameters={},strength=1))
    for k in ('masterCurve','redCurve','greenCurve','blueCurve'):state[k]=[0,.25,.5,.75,1]
    for k in ('hslHue','hslSaturation','hslLuminance'):state[k]=[0]*8
    d=dict(passed=True,error='',phase=15,elapsed_ms=2500,deadline_ms=60000,source_commit=commit,
           actual_Main_App_PhotoCanvas=True,controller_on_actual_GUI_thread=True,language='en_US'if mode=='cpu'else'zh_CN',
           actual_qpa=qpa,actual_backend={'cocoa':'Metal','windows':'Direct3D 11','xcb':'OpenGL'}[qpa]if mode=='gpu'else'CPU',GPU_active=mode=='gpu',
           settings_isolated=True,source_path='/owned/source.png',source_sha256='c'*64,source_unchanged=True,
           initial_whole_Develop=state,final_whole_Develop=deepcopy(state),initial_history=[{'action':'original','current':False},{'action':'adjustment','current':True}],
           source_key='70',render_revision='14',profile_path='/owned/output.icc',profile_sha256=profile,profile_model='RGB',
           header_text='SOFT PROOF'if mode=='cpu'else'软打样',presentation_only=True,proof_independent_CPU_reference=True,
           exact_scopes_preserved=True,original_GPU_parity_covered_by_separate_suite=True,durable_edit_history_preserved=True,
           physical_device_verified=False,real_printer_profile_verified=False,native_float_RAW_decoder=False,product_development_complete=False,
           export=dict(proof_enabled_during_export=True,RGBA16_exact=True,ICC_srgb=True,pixels=pixels),
           phase_checkpoints=[dict(phase=i,at_ms=i*100)for i in range(15)])
    d['proof_command_trace_passed']=True
    d['proof_commands']=[{'command':'proof.profile','source':d['profile_path']},{'command':'proof.enable','enabled':True},
        {'command':'proof.paper','enabled':True},{'command':'proof.gamut-warning','enabled':True},
        {'command':'proof.black-point','enabled':False},{'command':'proof.refresh'},
        {'command':'proof.enable','enabled':False},{'command':'proof.enable','enabled':True}]
    d['live_invariants']=dict(source_key='70',revision='14',pixels=307*211,**{k:True for k in ('source_same','revision_same','state_same','history_same','red_same','green_same','blue_same','luma_same','shadow_same','highlight_same')})
    d['final_history']=deepcopy(d['initial_history'])
    d['scope']=dict(status='Full-resolution statistics'if mode=='cpu'else'全分辨率统计',pixels=pixels,bins=1024,**{key:[pixels]+[0]*1023 for key in ('red','green','blue','luma')})
    targets=['softProofPanelToggle','softProofEnable','softProofPaper','softProofGamut','softProofBlackPoint','softProofRefresh','softProofEnable','softProofEnable']
    d['pointers']=[dict(target=key,x=100,y=200,QPA_mouse_route=True,source_key='50',render_revision='8',post_event_render_revision='8',at_ms=i*50+20)for i,key in enumerate(targets)]
    d['proofs']=[]
    for i,role in enumerate(('relative','paper','gamut','black-point-off','monitor')):
        f=dict(enabled=True,available=True,state='ready',error='',target='custom',presentation_only=True,alpha_exact=True,export_or_scopes_modified=False,
               monitor_icc_applied=False,input='gamma1.8-prophoto-rgba16',output='encoded-srgb-rgba16',proof_profile_model='RGB',proof_profile_key='soft-proof-icc-v1:'+profile,
               profile_key='soft-proof-icc-v1:'+profile,proof_width=307,proof_height=211,pixels=pixels,source_scaled_for_proof=False,presentation_reserved_bytes=pixels*32,
               cpu_operation_reserved_bytes=90*1024*1024,gpu_staging_source_key='50',parameter_revision='8',prepared_source_key='40',generation=str(9+i),intent=1,
               simulate_paper=i>=1,gamut_warning=i>=2,black_point_compensation=i<3,monitor_lut_key='owned-proof-invert'if i==4 else'identity-srgb')
        d['proofs'].append(dict(role=role,path='/owned/'+role+'.png',RGBA16_exact=True,actual_pixels_sha256=str(i+1)*64,expected_pixels_sha256=str(i+1)*64,facts=f))
    d['window_capture_admission']=dict(accepted=True,before_any_input=True,resize_count=0,DPR=2,logical_width=1180,logical_height=780,physical_width=2360,physical_height=1560,presented_frames=3)
    d['window_captures']=[dict(role=role,phase=phase,source_key='50',render_revision='8',DPR=2,logical_width=1180,logical_height=780,
                               physical_width=2360,physical_height=1560,presented_frames=3,at_ms=phase*100+20)for role,phase in zip(('relative','paper','gamut','off'),(2,3,4,8))]
    d['alpha_composition']=dict(passed=True,no_double_composition=True,partial_alpha=32768,tolerance_U8=3,max_U8_error=1,expected=[10,50,70],observed=[11,49,71])
    d['diagnostic']=dict(proof=dict(d['proofs'][3]['facts'],parameter_revision='14',gpu_staging_source_key='70'))
    return d


def diagnostic_fixture(mode='gpu'):
    d=report_fixture(mode);manifest={'adjustments':deepcopy(d['initial_whole_Develop'])}
    stages=dict(parameter_revision=14,gpu_context={'source_cache_key':'70'},gpu_working_output={'available':mode=='gpu'})
    if mode=='gpu':stages['gpu_working_output'].update(producer='gpu-compute',numeric_source_policy='none',monitor_icc=False,pixels=307*211,bytes=307*211*16,nonfinite_rgba=[0]*4)
    performance={'values':{'soft_proof':deepcopy(d['diagnostic']['proof'])}}
    return d,manifest,stages,performance


class SoftProofEvidenceTests(unittest.TestCase):
    def test_shared_proof_commands_require_a_true_boolean_flag(self):
        for value in (False,1,None,'true'):
            d=report_fixture();d['proof_command_trace_passed']=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')
        d=report_fixture();del d['proof_command_trace_passed']
        with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_missing_reordered_or_extra_presentation_commands_are_rejected(self):
        for commands in (None,[],report_fixture()['proof_commands'][1:],list(reversed(report_fixture()['proof_commands'])),report_fixture()['proof_commands']+[{'command':'proof.refresh'}]):
            d=report_fixture();d['proof_commands']=commands
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')
        d=report_fixture();del d['proof_commands']
        with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_presentation_commands_bind_the_loaded_profile_and_exact_fields(self):
        for index,key,value in ((0,'source','/different/profile.icc'),(2,'overwrite',True),(5,'enabled',True),(6,'command','develop.set')):
            d=report_fixture();d['proof_commands'][index][key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_presentation_boolean_numbers_cannot_impersonate_native_commands(self):
        for index in (1,2,3,4,6,7):
            d=report_fixture();d['proof_commands'][index]['enabled']=int(d['proof_commands'][index]['enabled'])
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_honest_original_cpu_gpu_reports_and_frozen_diagnostics(self):
        for mode in ('cpu','gpu'):
            for qpa in ('cocoa','windows','xcb'):self.assertTrue(validate_report(report_fixture(mode,qpa),mode,qpa,'a'*40))
            self.assertTrue(validate_diagnostic(*diagnostic_fixture(mode),mode))

    def test_changed_edits_or_history_and_incomplete_snapshots_are_rejected(self):
        for key,value in [('final_whole_Develop',{}),('initial_whole_Develop',{}),('final_history',[]),('initial_history',[]),('exact_scopes_preserved',False)]:
            with self.subTest(key=key):
                d=report_fixture();d[key]=value
                with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_old_or_preview_statistics_cannot_be_admitted_as_current_full(self):
        for status in ['更新中（旧统计）','GPU 预览统计','全分辨率统计 · 全分辨率更新中']:
            d=report_fixture('gpu','windows');d['scope']['status']=status
            with self.assertRaises(ValueError):validate_report(d,'gpu','windows')

    def test_stale_cpu_gpu_source_or_revision_and_unadmitted_memory_are_rejected(self):
        for key,value in [('gpu_staging_source_key','51'),('parameter_revision','7'),('prepared_source_key','0'),('presentation_reserved_bytes',0),('cpu_operation_reserved_bytes',0),('source_scaled_for_proof',True)]:
            with self.subTest(key=key):
                d=report_fixture();d['proofs'][2]['facts'][key]=value
                with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_clipped_input_modified_export_and_fake_monitor_claims_are_rejected(self):
        for key,value in [('input','encoded-srgb-rgba16'),('export_or_scopes_modified',True),('monitor_icc_applied',True),('proof_profile_key','wrong'),('monitor_lut_key','wrong')]:
            d=report_fixture();d['proofs'][2]['facts'][key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')
        d=report_fixture();d['export']['proof_enabled_during_export']=False
        with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_noop_paper_or_mismatched_proof_pixels_are_rejected(self):
        for change in ('noop','mismatch'):
            d=report_fixture();p=d['proofs'][1]
            if change=='noop':p['actual_pixels_sha256']=p['expected_pixels_sha256']=d['proofs'][0]['actual_pixels_sha256']
            else:p['actual_pixels_sha256']='f'*64
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_double_alpha_composition_and_relaxed_tolerance_are_rejected(self):
        for key,value in [('no_double_composition',False),('tolerance_U8',5),('max_U8_error',4),('observed',[30,49,71])]:
            d=report_fixture();d['alpha_composition'][key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_cropped_or_stale_window_or_fake_qpa_input_is_rejected(self):
        for key,value in [('physical_width',2300),('presented_frames',2),('phase',7),('source_key','49'),('at_ms',0)]:
            d=report_fixture();d['window_captures'][1][key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')
        d=report_fixture();d['pointers'][2]['post_event_render_revision']='9'
        with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_gpu_diagnostic_cpu_upload_or_stale_proof_is_rejected(self):
        for key,value in [('producer','cpu-reference'),('numeric_source_policy','signed-source'),('monitor_icc',True)]:
            args=list(diagnostic_fixture());args[2]['gpu_working_output'][key]=value
            with self.assertRaises(ValueError):validate_diagnostic(*args,'gpu')
        args=list(diagnostic_fixture());args[3]['values']['soft_proof']['parameter_revision']='13'
        with self.assertRaises(ValueError):validate_diagnostic(*args,'gpu')

    def test_relaxed_deadline_wrong_commit_and_unsupported_acceptance_are_rejected(self):
        for key,value in [('elapsed_ms',60000),('deadline_ms',120000),('physical_device_verified',True),('real_printer_profile_verified',True),('product_development_complete',True),('GPU_active',False)]:
            d=report_fixture();d[key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')
        with self.assertRaises(ValueError):validate_report(report_fixture(),'gpu','cocoa','d'*40)

    def test_live_final_source_revision_and_scopes_cannot_drift(self):
        for key,value in [('source_same',False),('revision_same',False),('red_same',False),('highlight_same',False),('revision','13')]:
            d=report_fixture();d['live_invariants'][key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_occupied_report_namespace_never_launches_or_replaces_original(self):
        with tempfile.TemporaryDirectory()as temp:
            root=Path(temp);binary=root/'App';binary.write_bytes(b'owned');report=root/'proof.json';original=root/'proof.native.json';original.write_bytes(b'original failed evidence')
            with patch('ValidateSoftProof.subprocess.run')as run:
                with self.assertRaises(ValueError):validate_soft_proof(binary,{},root/'home',report,'cpu')
                run.assert_not_called()
            self.assertEqual(original.read_bytes(),b'original failed evidence')


if __name__=='__main__':unittest.main()
