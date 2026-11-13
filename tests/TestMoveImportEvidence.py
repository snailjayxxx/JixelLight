"""Reject stale, unreviewed or falsely attributed source-retirement evidence."""
from copy import deepcopy
from contextlib import closing
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ValidateMoveImport import validate_report,validate_diagnostic,validate_catalog,validate_move_import,validate_operation_identities
from TestSoftProofEvidence import report_fixture as parent


def report_fixture(mode='gpu',qpa='cocoa'):
    initial=parent(mode,qpa)['initial_whole_Develop'];base='C:/owned/'if qpa=='windows'else'/owned/'
    history=[dict(action='original',current=False),dict(action='adjustment',current=True)]
    d=dict(passed=True,error='',phase=12,elapsed_ms=2000,deadline_ms=60000,source_commit='a'*40,
           actual_Main_App_PhotoCanvas=True,controller_on_actual_GUI_thread=True,language='en_US'if mode=='cpu'else'zh_CN',actual_qpa=qpa,
           actual_backend={'cocoa':'Metal','windows':'Direct3D 11','xcb':'OpenGL'}[qpa]if mode=='gpu'else'CPU',GPU_active=mode=='gpu',settings_isolated=True,
           actual_system_trash=True,controlled_trash_service=False,owned_fixture_file_picker_setup=True,native_file_picker_verified=False,
           forced_process_crash_verified=False,owned_interrupted_copy_fixture=True,physical_device_verified=False,native_float_RAW_decoder=False,product_development_complete=False,
           baseline_path=base+'case.native.json.baseline-source.png',baseline_sha256='b'*64,unselected_original_unchanged=True,
           initial_whole_Develop=initial,final_whole_Develop=deepcopy(initial),initial_history=history,final_history=deepcopy(history),
           initial_source_key='50',initial_render_revision='8',final_source_key='70',final_render_revision='14',catalog_count=4,
           reopened_source_key='70',reopened_render_revision='14',reopen_settled_ms=220,reopened_owner_current=True,
           project_path=base+'case.native.json.project-root/Owned.jlp',durable_identities_verified=True,project_reopened=True,
           export=dict(RGBA16_exact=True,ICC_srgb=True),phase_checkpoints=[dict(phase=i,at_ms=i*100)for i in range(12)],
           transfer_operation_ids=['c59c39df-320d-4b8b-89ac-9307454d0208','3e3ee7a6-cc2d-439e-ae97-582f997b15bc'],
           stale_transfer_cancel_refused=True,idle_transfer_operation_id='')
    d['records']=[dict(index=i,source=base+'JixelLight-owned-move-unit/JixelLight-owned-unit-'+str(i)+'.png',before=base+'case.native.json.source-'+str(i)+'-before.png',
                       destination=base+'case.native.json.destination/JixelLight-owned-unit-'+str(i)+'.png',sha256=str(i+1)*64,bytes=1000,state='trashed',trash_path_reported=True,
                       source_absent_after_system_trash=True,durable_identity=True,journal=base+'case.native.json.destination/.jixellight-move-'+('first'if i<2 else'recovered')+'/move.json',journal_sha256='d'*64)for i in range(3)]
    d['review']=dict(read_only_before_click=True,visible_rows=1,source_still_present=True,catalog_count_before=3,record_digest='e'*64)
    d['pointers']=[dict(target=target,text=text,QPA_mouse_route=True,x=100,y=200,source_key='50',post_event_source_key='50',render_revision='8',post_event_render_revision='8',at_ms=time)
                   for target,text,time in [('copyImportApply','Start move'if mode=='cpu'else'开始移动',220),('moveImportResume','Verify and resume move'if mode=='cpu'else'核对并继续移动',520)]]
    d['scope']=dict(status='Full-resolution statistics'if mode=='cpu'else'全分辨率统计',pixels=307*211,bins=1024,four_histograms_exact=True,shadow_clip_percent=0,highlight_clip_percent=0,
                    **{key:[307*211]+[0]*1023 for key in ('red','green','blue','luma')})
    d['window_capture_admission']=dict(accepted=True,before_any_input=True,resize_count=0,DPR=2,logical_width=1180,logical_height=780,physical_width=2360,physical_height=1560,presented_frames=3)
    d['window_captures']=[dict(role=role,phase=phase,DPR=2,logical_width=1180,logical_height=780,physical_width=2360,physical_height=1560,presented_frames=3,at_ms=phase*100+20,
                               source_key='70'if phase==7 else'50',render_revision='14'if phase==7 else'8',preview_ready=True,source_available=True,loading=False,rendering=False)for role,phase in zip(('moved','recovery','recovered','reopened'),(3,5,6,7))]
    d['move']=dict(phase='done',retirement_backend='system-trash',busy=False,requested=1,copied=1,retired=1,cancelled=False,error='')
    return d


def diagnostic_fixture(mode='gpu'):
    d=report_fixture(mode);manifest={'adjustments':deepcopy(d['initial_whole_Develop'])}
    stages=dict(parameter_revision=14,gpu_context={'source_cache_key':'70'},gpu_working_output={'available':mode=='gpu'})
    if mode=='gpu':stages['gpu_working_output'].update(producer='gpu-compute',numeric_source_policy='none',monitor_icc=False,pixels=307*211,bytes=307*211*16,nonfinite_rgba=[0]*4)
    return d,manifest,stages,{'values':{'move_import':deepcopy(d['move'])}}


def catalog_fixture(root):
    d=report_fixture();project=root/'owned.jlp';project.mkdir();d['project_path']=str(project);d['baseline_path']=str(root/'baseline.png')
    for i,row in enumerate(d['records']):row['destination']=str(root/('copy-'+str(i)+'.png'))
    db=project/'Project.db'
    with closing(sqlite3.connect(db))as c,c:
        c.execute('CREATE TABLE photos(path TEXT PRIMARY KEY,adjustment_json TEXT)');c.execute('CREATE TABLE original_identity(source_key TEXT PRIMARY KEY,schema INTEGER,size_bytes INTEGER,sha256 TEXT)')
        c.execute('INSERT INTO photos VALUES(?,?)',(d['baseline_path'],json.dumps(dict(d['initial_whole_Develop'],_history={'cursor':1,'entries':[]}))))
        for row in d['records']:
            c.execute('INSERT INTO photos VALUES(?,?)',(row['destination'],'{}'));c.execute('INSERT INTO original_identity VALUES(?,?,?,?)',(row['destination'],1,row['bytes'],row['sha256']))
    return d,db


class MoveImportEvidenceTests(unittest.TestCase):
    def test_reused_noncanonical_and_missing_operation_identities_are_refused(self):
        original=report_fixture()['transfer_operation_ids']
        for ids in ([],original[:1],[original[0]]*2,[original[0].upper(),original[1]],
                    ['00000000-0000-0000-0000-000000000000',original[1]],[True,original[1]],[1,2]):
            d=report_fixture();d['transfer_operation_ids']=ids
            with self.assertRaises(ValueError):validate_operation_identities(d)

    def test_numeric_or_false_cancellation_refusal_and_busy_completion_are_refused(self):
        for key,value in [('stale_transfer_cancel_refused',False),('stale_transfer_cancel_refused',1),('idle_transfer_operation_id','c59c39df-320d-4b8b-89ac-9307454d0208')]:
            d=report_fixture();d[key]=value
            with self.assertRaises(ValueError):validate_operation_identities(d)

    def test_honest_modes_platforms_and_frozen_diagnostics(self):
        for mode in ('cpu','gpu'):
            for qpa in ('cocoa','windows','xcb'):self.assertTrue(validate_report(report_fixture(mode,qpa),mode,qpa,'a'*40))
            self.assertTrue(validate_diagnostic(*diagnostic_fixture(mode),mode))

    def test_fake_trash_or_missing_durable_identity_or_live_source_is_rejected(self):
        for key,value in [('actual_system_trash',False),('controlled_trash_service',True),('durable_identities_verified',False)]:
            d=report_fixture();d[key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')
        for key,value in [('state','copied'),('source_absent_after_system_trash',False),('durable_identity',False),('bytes',0)]:
            d=report_fixture();d['records'][1][key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_unknown_or_duplicate_source_or_transaction_is_rejected(self):
        for change in ('foreign','duplicate','index_bool','same_journal'):
            d=report_fixture()
            if change=='foreign':d['records'][0]['source']='/real/user/photo.png'
            elif change=='duplicate':d['records'][1]['destination']=d['records'][0]['destination']
            elif change=='index_bool':d['records'][0]['index']=False
            else:d['records'][2]['journal']=d['records'][0]['journal']
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_changed_original_edit_history_or_catalog_is_rejected(self):
        for key,value in [('final_whole_Develop',{}),('final_history',[]),('unselected_original_unchanged',False),('catalog_count',3),('project_reopened',False)]:
            d=report_fixture();d[key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_transient_reopened_owner_or_busy_capture_cannot_pass(self):
        for key,value in [('reopened_source_key','50'),('reopened_render_revision','12'),('reopen_settled_ms',199),('reopened_owner_current',False)]:
            d=report_fixture();d[key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')
        for key,value in [('preview_ready',False),('source_available',False),('loading',True),('rendering',True)]:
            d=report_fixture();d['window_captures'][-1][key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_unreviewed_or_stale_recovery_or_fake_qpa_mutation_is_rejected(self):
        for key,value in [('read_only_before_click',False),('visible_rows',True),('source_still_present',False),('record_digest','bad')]:
            d=report_fixture();d['review'][key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')
        d=report_fixture();d['pointers'][1]['post_event_render_revision']='9'
        with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_cropped_stale_or_unpresented_window_is_rejected(self):
        for key,value in [('physical_width',2000),('source_key','50'),('presented_frames',2),('at_ms',0)]:
            d=report_fixture();d['window_captures'][3][key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_relaxed_deadline_wrong_build_and_unsupported_acceptance_is_rejected(self):
        for key,value in [('deadline_ms',120000),('elapsed_ms',60000),('native_file_picker_verified',True),('forced_process_crash_verified',True),('physical_device_verified',True),('product_development_complete',True)]:
            d=report_fixture();d[key]=value
            with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')
        with self.assertRaises(ValueError):validate_report(report_fixture(),'gpu','cocoa','f'*40)

    def test_frozen_gpu_cpu_upload_or_changed_transfer_facts_are_rejected(self):
        args=list(diagnostic_fixture());args[2]['gpu_working_output']['producer']='cpu-reference'
        with self.assertRaises(ValueError):validate_diagnostic(*args,'gpu')
        args=list(diagnostic_fixture());args[3]['values']['move_import']['phase']='copied'
        with self.assertRaises(ValueError):validate_diagnostic(*args,'gpu')

    def test_independent_read_only_catalog_accepts_matching_identities(self):
        with tempfile.TemporaryDirectory()as temp:
            d,db=catalog_fixture(Path(temp));before=db.read_bytes();self.assertTrue(validate_catalog(d));self.assertEqual(db.read_bytes(),before)

    def test_catalog_reader_is_closed_with_retained_references_on_success_and_failure(self):
        original=sqlite3.connect
        for invalid in [False,True]:
            with tempfile.TemporaryDirectory()as temp:
                d,db=catalog_fixture(Path(temp));connections=[]
                if invalid:d['records'][0]['sha256']='f'*64
                def connect(*args,**kwargs):
                    connection=original(*args,**kwargs);connections.append(connection);return connection
                with patch('ValidateMoveImport.sqlite3.connect',side_effect=connect):
                    if invalid:
                        with self.assertRaises(ValueError):validate_catalog(d)
                    else:self.assertTrue(validate_catalog(d))
                self.assertEqual(len(connections),1)
                with self.assertRaises(sqlite3.ProgrammingError):connections[0].execute('SELECT 1')

    def test_old_or_preview_original_statistics_are_rejected(self):
        for status in ['更新中（旧统计）','GPU 预览统计','全分辨率统计 · 全分辨率更新中']:
            d=report_fixture('gpu','windows');d['scope']['status']=status
            with self.assertRaises(ValueError):validate_report(d,'gpu','windows')

    def test_independent_catalog_rejects_changed_identity_state_or_missing_source(self):
        for change in ('sha','state','missing'):
            with tempfile.TemporaryDirectory()as temp:
                d,db=catalog_fixture(Path(temp))
                with closing(sqlite3.connect(db))as c,c:
                    if change=='sha':c.execute('UPDATE original_identity SET sha256=? WHERE source_key=?',('f'*64,d['records'][1]['destination']))
                    elif change=='state':c.execute('UPDATE photos SET adjustment_json=? WHERE path=?',(json.dumps(dict(d['initial_whole_Develop'],exposure=.3,_history={})),d['baseline_path']))
                    else:c.execute('DELETE FROM photos WHERE path=?',(d['records'][1]['destination'],))
                with self.assertRaises(ValueError):validate_catalog(d)

    def test_occupied_namespace_never_executes_or_replaces_original_evidence(self):
        with tempfile.TemporaryDirectory()as temp:
            root=Path(temp);binary=root/'App';binary.write_bytes(b'owned');original=root/'move.native.json';original.write_bytes(b'original failed evidence')
            with patch('ValidateMoveImport.subprocess.run')as run:
                with self.assertRaises(ValueError):validate_move_import(binary,{},root/'home',root/'move.json','cpu')
                run.assert_not_called()
            self.assertEqual(original.read_bytes(),b'original failed evidence')

if __name__=='__main__':unittest.main()
