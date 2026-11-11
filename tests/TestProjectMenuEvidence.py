"""New typed/dispatch transport controls; these never establish actual App acceptance."""
from __future__ import annotations
from copy import deepcopy
from pathlib import Path
import ast,json,os,subprocess,sys,tempfile,textwrap,unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import ProjectMenuEvidence as evidence
import ValidateProjectMenu as wrapper

def fixture():
    rows=[]
    for i,(language,width,height)in enumerate(evidence.CASES):
        rows.append({'language':language,'window_width':width,'window_height':height,'menu_text':evidence.TEXT[language],'actual_menu_width':214,'label_width':200,'label_implicit_width':190,'actual_font_text_width':190,'actual_truncated':False,'menu_global_x':100,'menu_global_y':80,'button_bottom_global_x':100,'button_bottom_global_y':80,'first_menu_global_x':100,'first_menu_global_y':80,'first_button_bottom_global_x':100,'first_button_bottom_global_y':80,'actual_button_parent':True,'anchor_error_at_most2':True,'two_distinct_button_pointer_origins_stable':True,'first_pointer_fraction':.25,'second_pointer_fraction':.75,'capture':{'path':f'/owned/raw.json.case-{i}.png','width':width,'height':height,'sha256':'a'*64}})
    return {'schema':2,'mode':'project-menu-layout-only-v2','passed':True,'error':'','source_commit':'abcdef0','qt_version':'6.8.3','GUI_deadline_ms':60000,'GUI_elapsed_ms':11,'actual_qpa':'cocoa','actual_backend':6,'actual_backend_name':'Metal','actual_DPR':1,'GUI_thread':'1a','actual_RHI_thread':'2b','actual_afterFrameEnd_count':5,'actual_window_system_pointer':True,'menu_close_is_controlled_cleanup':True,'settings_isolated':True,'image_import_or_XMP_merge_exercised':False,'physical_input_or_monitor_calibration_claim':False,'Root_or_CI_or_F0_F6_acceptance_claim':False,'cases':rows}

class ProjectMenuEvidenceControls(unittest.TestCase):
    def reject(self,mutate):
        data=fixture();mutate(data)
        with self.assertRaises((AssertionError,ValueError,KeyError,TypeError)):evidence.validate(data,expected_commit='abcdef0123456')

    def test_complete_schema2_and_three_declared_backends(self):
        for qpa,(name,enum)in evidence.BACKENDS.items():
            data=fixture();data.update(actual_qpa=qpa,actual_backend=enum,actual_backend_name=name)
            out=evidence.validate(data,expected_commit='abcdef0123456');self.assertEqual(out['case_count'],4);self.assertEqual(out['actual_backend'],name);self.assertFalse(out['physical_input_or_complete_UI_claim'])

    def test_compile_stamp_qt_and_renderer_provenance(self):
        for key,value in [('schema',1),('mode','project-menu-layout-only-v1'),('source_commit','unknown'),('source_commit','1234567'),('source_commit',True),('qt_version','6.11.0'),('actual_qpa','offscreen'),('actual_backend',True),('actual_backend',4),('actual_backend_name','OpenGL')]:
            self.reject(lambda d,k=key,v=value:d.__setitem__(k,v))

    def test_first_and_second_numeric_origins_are_independent(self):
        self.reject(lambda d:d['cases'][0].__setitem__('first_menu_global_x',101))
        self.reject(lambda d:d['cases'][0].__setitem__('first_button_bottom_global_y',83))
        self.reject(lambda d:d['cases'][0].__setitem__('button_bottom_global_x',97))
        self.reject(lambda d:d['cases'][0].__setitem__('first_pointer_fraction',.75))
        self.reject(lambda d:d['cases'][0].pop('first_menu_global_y'))

    def test_new_numeric_witnesses_reject_bool_and_nonfinite(self):
        for key in ['first_menu_global_x','first_menu_global_y','first_button_bottom_global_x','first_button_bottom_global_y']:
            for value in [True,float('nan'),float('inf'),-float('inf')]:self.reject(lambda d,k=key,v=value:d['cases'][0].__setitem__(k,v))
        for key,value in [('GUI_elapsed_ms',60000),('GUI_elapsed_ms',True),('actual_DPR',True),('actual_afterFrameEnd_count',0),('actual_afterFrameEnd_count',True)]:self.reject(lambda d,k=key,v=value:d.__setitem__(k,v))
        self.reject(lambda d:d.__setitem__('actual_RHI_thread',d['GUI_thread']))

    def test_ordered_contexts_and_complete_labels(self):
        self.reject(lambda d:d['cases'].pop())
        self.reject(lambda d:d['cases'].append(deepcopy(d['cases'][0])))
        self.reject(lambda d:d['cases'].__setitem__(1,deepcopy(d['cases'][0])))
        self.reject(lambda d:d['cases'][0].__setitem__('window_width',1540))
        self.reject(lambda d:d['cases'][0].__setitem__('menu_text','Import XMP…'))

    def test_layout_and_honest_scope_remain_strict(self):
        for key in ['actual_button_parent','anchor_error_at_most2','two_distinct_button_pointer_origins_stable']:self.reject(lambda d,k=key:d['cases'][0].__setitem__(k,False))
        for key,value in [('actual_truncated',True),('label_width',100),('actual_menu_width',1190),('label_implicit_width',215)]:self.reject(lambda d,k=key,v=value:d['cases'][0].__setitem__(k,v))
        for key in ['image_import_or_XMP_merge_exercised','physical_input_or_monitor_calibration_claim','Root_or_CI_or_F0_F6_acceptance_claim']:self.reject(lambda d,k=key:d.__setitem__(k,True))
        for key in ['actual_window_system_pointer','settings_isolated','menu_close_is_controlled_cleanup']:self.reject(lambda d,k=key:d.__setitem__(k,False))

    def test_json_rejects_duplicate_and_nonfinite_even_unknown_fields(self):
        for text in ['{"source_commit":"abcdef0","source_commit":"1234567"}','{"ignored":{"a":NaN}}','{"ignored":1e309}']:
            with self.assertRaises(ValueError):evidence.decode_report(text)
        self.assertEqual(evidence.decode_report('{"ordinary":1.25}')['ordinary'],1.25)

    def test_command_and_every_output_suffix_refuse_existing_aliases(self):
        self.assertEqual(wrapper.command(Path('/owned/App'),Path('/owned/raw.json')),[str(Path('/owned/App')),'--project-menu-layout-smoke-report',str(Path('/owned/raw.json'))])
        with tempfile.TemporaryDirectory()as directory:
            base=Path(directory);exe=base/'App';exe.write_bytes(b'owned input');home=base/'home';report=base/'report.json'
            self.assertEqual(len(wrapper.outputs(report)),8)
            for path in wrapper.outputs(report):
                path.write_bytes(b'owned existing')
                with self.assertRaises(ValueError):wrapper.preflight(exe,home,report)
                path.unlink()
            link=wrapper.outputs(report)[4];original=Path.is_symlink
            # A branch spy avoids requiring a Windows symlink privilege for a
            # pure admission control. This is not real Windows filesystem QA.
            with patch.object(Path,'is_symlink',lambda path:path==link or original(path)):
                with self.assertRaises(ValueError):wrapper.preflight(exe,home,report)
            os.link(exe,link)
            with self.assertRaises(ValueError):wrapper.preflight(exe,home,report)
            link.unlink();self.assertEqual(exe.read_bytes(),b'owned input')

    def run_failure(self,kind):
        with tempfile.TemporaryDirectory()as directory:
            base=Path(directory);exe=base/'App';exe.write_bytes(b'owned App marker');report=base/'menu.json'
            raw_bytes=b'{"schema":2'if kind=='timeout'else b'{"schema":1,"passed":true}'
            def fake(command,**kwargs):
                raw=Path(command[-1]);raw.write_bytes(raw_bytes)
                if kind=='timeout':raise subprocess.TimeoutExpired(command,70,output=b'partial\xffstdout',stderr=b'partial stderr')
                return subprocess.CompletedProcess(command,1 if kind=='process'else 0,b'original\xffstdout',b'original stderr')
            with patch.object(wrapper.subprocess,'run',side_effect=fake),patch.object(wrapper,'png',side_effect=AssertionError('No PNG audit before process/schema refusal')):
                result=wrapper.validate_project_menu(exe,{},base/'home',report,expected_commit='abcdef0')
            self.assertFalse(result['passed']);self.assertFalse(result['production_accepted']);self.assertFalse(result['actual_installed_package_or_current_CI_acceptance']);self.assertTrue(result['original_process_streams_byte_exact']);self.assertEqual(Path(str(report)+'.raw.json').read_bytes(),raw_bytes)
            expected=b'partial\xffstdout'if kind=='timeout'else b'original\xffstdout';self.assertEqual(Path(str(report)+'.stdout').read_bytes(),expected);self.assertEqual(exe.read_bytes(),b'owned App marker')
            return result

    def test_wrapper_failed_process_preserves_original_bytes(self):self.run_failure('process')

    def test_wrapper_legacy_schema_is_not_upgraded_or_accepted(self):self.run_failure('schema')

    def test_wrapper_timeout_preserves_partial_raw_and_streams(self):
        result=self.run_failure('timeout');self.assertTrue(result['process_timed_out']);self.assertTrue(result['timeout_streams_are_partial'])

    def test_package_new_block_appends_only_one_after_existing_entries(self):
        source=(Path(__file__).resolve().parents[1]/'tools/ValidatePackage.py').read_text();block=source.split('    # BEGIN project-menu-layout package mode; all original65 remain byte-exact.\n',1)[1].split('    # END project-menu-layout package mode.\n',1)[0]
        calls=[];results=[{'mode':'prior-entry','source_commit':'abcdef0'}]
        def fake(*args,**kwargs):calls.append((args,kwargs));return{'mode':'project-menu-layout','passed':True,'synthetic_dispatch_only':True}
        with tempfile.TemporaryDirectory()as directory:
            scope={'Path':Path,'tempfile':tempfile,'results':results,'validate_project_menu':fake,'executable':Path('/owned/installed/App'),'environment':{'owned':'environment'},'reports':Path(directory)}
            exec(compile(textwrap.dedent(block),'new_menu_registration_block','exec'),scope)
        self.assertEqual(len(calls),1);self.assertEqual(results[0]['mode'],'prior-entry');self.assertEqual(results[-1]['mode'],'project-menu-layout');self.assertEqual(calls[0][1]['expected_commit'],'abcdef0')

if __name__=='__main__':unittest.main()
