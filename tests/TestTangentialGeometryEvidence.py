"""New tangential App evidence metadata: no GUI, rendering or old PNG controls."""
from copy import deepcopy
import importlib.util
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ValidateTangentialGeometry import validate_report, DEVELOP_FIELDS


def fixture(mode='gpu',qpa='cocoa'):
    state={k:0 for k in DEVELOP_FIELDS}
    for k in ('masterCurve','redCurve','greenCurve','blueCurve'):state[k]=[0,.25,.5,.75,1]
    for k in ('hslHue','hslSaturation','hslLuminance'):state[k]=[0]*8
    state['look']={'schema':1,'mode':'off','code':'','strength':1,'parameters':{}}
    state['geometry']={'schema':1,'x':0,'y':0,'width':1,'height':1,'quarterTurns':0,'flipHorizontal':False,'flipVertical':False}
    first=deepcopy(state);first['geometry'].update(schema=4,straighten=0,perspectiveHorizontal=0,perspectiveVertical=0,distortion=0,redCa=0,blueCa=0,tangentialP1=.011,tangentialP2=0)
    active=deepcopy(first);active['geometry']['tangentialP2']=-.011
    source='/owned/test.native.json.source.png'
    d={'passed':True,'error':'','phase':14,'actual_Main_App_PhotoCanvas':True,'controller_on_actual_GUI_thread':True,'source_unchanged':True,'settings_isolated':True,'physical_device_verified':False,'native_float_RAW_decoder':False,'normal_Root_or_CI_accepted':False,'product_development_complete':False,'GPU_full_readback_fingerprint_only_not_new_perpixel40proof':True,'GPU_active':mode=='gpu','language':'en_US'if mode=='cpu'else'zh_CN','actual_qpa':qpa,'actual_backend':'CPU parallel'if mode=='cpu'else{'cocoa':'GPU · Metal · synthetic','windows':'GPU · Direct3D 11 · synthetic','xcb':'GPU · OpenGL · synthetic'}[qpa],'elapsed_ms':1200,'deadline_ms':60000,'DPR':1.5,'source_commit':'1234567','source_sha256':'a'*64,'source_path':source,'project_path':'/owned/test.jlp','initial_whole_Develop':state,'first_whole_Develop':first,'active_whole_Develop':active,'staging':{'complete_pixels':20,'corrected_width':5,'corrected_height':4,'bit_exact':True,'actual_FP32_staging_sha256':'b'*64,'expected_FP32_staging_sha256':'b'*64,'ICC_sha256':'c'*64},'scope':{'pixels':20,'bins':1024,'four_histograms_exact':True,'clipping_exact':True,**{c:[20]+[0]*1023 for c in ('red','green','blue','luma')}},'scope_plots':[{'mode':m,'current':True,'full_resolution':True,'pixels':'20'}for m in ('waveform','parade','vectorscope')],'prepare_cache_hit_delta':4,'phase_checkpoints':[{'phase':i,'at_ms':i*50}for i in range(14)],'pointers':[{'target':m,'QPA_mouse_route':True,'source_key':str(30+i),'render_revision':str(6+i*4),'post_event_render_revision':str(7+i*4),'post_event_source_key':'0','GUI_thread_id':'61','file':source,'local_x':100.,'local_y':200.,'global_x':300.,'global_y':400.,'at_ms':200+i*100}for i,m in enumerate(('tangentialP1','tangentialP2','resetGeometryCorrections'))],'stable_presented_warmups':[{'phase':i,'presented_frames':3,'signature':[str(30+i),str(6+i),1180,780,600,500]}for i in range(14)],'diagnostic':{'source_key':'99','request_revision':'40','sha256':'d'*64,'path':'/owned/test.native.json.diagnostic.zip'}}
    return d

class TangentialEvidenceTests(unittest.TestCase):
    def test_all_six_typed_backend_language_contexts(self):
        for qpa in ('cocoa','windows','xcb'):
            for mode in ('cpu','gpu'):
                with self.subTest(qpa=qpa,mode=mode):self.assertTrue(validate_report(fixture(mode,qpa),mode,qpa,'1234567'))

    def test_reject_changed_or_incomplete_current_evidence(self):
        edits=[(('passed',),False),(('error',),'incomplete'),(('phase',),13),(('phase',),14.0),(('language',),'en_US'),(('actual_qpa',),'xcb'),(('GPU_active',),False),(('actual_backend',),'GPU with no API'),(('actual_Main_App_PhotoCanvas',),False),(('controller_on_actual_GUI_thread',),False),(('settings_isolated',),False),(('source_unchanged',),False),(('DPR',),0),(('DPR',),float('nan')),(('elapsed_ms',),60000),(('elapsed_ms',),True),(('deadline_ms',),60000.0),(('source_commit',),'unknown'),(('source_sha256',),'a'*63),(('product_development_complete',),True),(('native_float_RAW_decoder',),True),(('physical_device_verified',),True),(('normal_Root_or_CI_accepted',),True),(('GPU_full_readback_fingerprint_only_not_new_perpixel40proof',),False),(('active_whole_Develop','look','strength'),.9),(('first_whole_Develop','geometry','tangentialP1'),0),(('active_whole_Develop','geometry','tangentialP2'),0),(('active_whole_Develop','geometry','schema'),4.0),(('active_whole_Develop','geometry','flipVertical'),0),(('staging','complete_pixels'),19),(('staging','bit_exact'),False),(('staging','actual_FP32_staging_sha256'),'e'*64),(('scope','bins'),1024.0),(('scope','red',0),19),(('scope_plots',1,'current'),False),(('scope_plots',2,'full_resolution'),False),(('prepare_cache_hit_delta',),0),(('phase_checkpoints',1,'phase'),0),(('phase_checkpoints',1,'at_ms'),1300),(('pointers',0,'source_key'),'0'),(('pointers',0,'render_revision'),'0'),(('pointers',0,'GUI_thread_id'),'0'),(('pointers',1,'GUI_thread_id'),'62'),(('pointers',2,'post_event_render_revision'),'1'),(('pointers',0,'file'),'/other'),(('pointers',0,'local_x'),float('nan')),(('pointers',0,'QPA_mouse_route'),False),(('stable_presented_warmups',1,'presented_frames'),2),(('stable_presented_warmups',1,'signature',0),'0'),(('diagnostic','source_key'),'0')]
        for path,value in edits:
            with self.subTest(path=path,value=value):
                d=fixture();cursor=d
                for key in path[:-1]:cursor=cursor[key]
                cursor[path[-1]]=value
                with self.assertRaises((ValueError,TypeError,KeyError,IndexError)):validate_report(d,'gpu','cocoa','1234567')
        self.assertEqual(len(edits),50)

    def test_reject_missing_whole_Develop_histogram_and_order(self):
        edits=[lambda d:d['active_whole_Develop'].pop('look'),lambda d:d['scope']['red'].pop(),lambda d:d['scope_plots'].reverse(),lambda d:d['pointers'].reverse(),lambda d:d['phase_checkpoints'].pop(),lambda d:d['stable_presented_warmups'].pop()]
        for edit in edits:
            d=fixture();edit(d)
            with self.assertRaises((ValueError,TypeError,KeyError,IndexError)):validate_report(d,'gpu','cocoa','1234567')

    def test_expected_compile_stamp_and_unsupported_QPA(self):
        for expected,qpa in [('7654321','cocoa'),('unknown','cocoa'),('1234567','offscreen')]:
            with self.assertRaises((ValueError,TypeError,KeyError,IndexError)):validate_report(fixture(),'gpu',qpa,expected)

if __name__=='__main__':unittest.main()
