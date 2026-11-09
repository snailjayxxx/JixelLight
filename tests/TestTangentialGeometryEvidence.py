"""New tangential App evidence metadata: no GUI, rendering or old PNG controls."""
from copy import deepcopy
import importlib.util
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ValidateTangentialGeometry import validate_report, validate_window_evidence, DEVELOP_FIELDS


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

def window_fixture(dpr=1.5, resize=False):
    d=fixture();d['DPR']=dpr;d['phase_checkpoints']=[{'phase':i,'at_ms':i*75}for i in range(14)]
    for row,at in zip(d['pointers'],[100,250,650]):row['at_ms']=at
    logical=[1180,720 if resize else 780];physical=[int(logical[0]*dpr+.5),int(logical[1]*dpr+.5)]
    for w in d['stable_presented_warmups']:w['signature'][2:4]=logical
    d['window_capture_admission']={'accepted':True,'phase':0,'input_started':False,'resize_count':int(resize),'initial_logical_size':[1180,780],
        'first_observed_physical_size':physical,'logical_width':logical[0],'logical_height':logical[1],'physical_width':physical[0],'physical_height':physical[1],
        'DPR':dpr,'presented_frames':3,'at_ms':50,'source_key':'30','render_revision':'6','GUI_thread_id':'61'}
    d['window_captures']=[]
    for role,phase in zip(('active','undo','redo','reset'),(4,7,8,9)):
        w=d['stable_presented_warmups'][phase]
        d['window_captures'].append({'role':role,'phase':phase,'path':d['source_path'].removesuffix('.source.png')+'.'+role+'.png',
            'logical_width':logical[0],'logical_height':logical[1],'physical_width':physical[0],'physical_height':physical[1],'DPR':dpr,
            'image_bits_per_pixel':32,'presented_frames':3,'source_key':w['signature'][0],'render_revision':w['signature'][1],'GUI_thread_id':'61','at_ms':phase*75+40})
    return d

class TangentialEvidenceTests(unittest.TestCase):
    def test_all_six_typed_backend_language_contexts(self):
        for qpa in ('cocoa','windows','xcb'):
            for mode in ('cpu','gpu'):
                with self.subTest(qpa=qpa,mode=mode):self.assertTrue(validate_report(fixture(mode,qpa),mode,qpa,'1234567'))

        # QUrl records forward slashes while an owned Windows report may use native separators.
        for mode in ('cpu','gpu'):
            d=fixture(mode,'windows');d['source_path']='D:\\owned\\test.native.json.source.png'
            for index,row in enumerate(d['pointers']):
                row['file']='D:/owned/test.native.json.source.png' if index%2==0 else d['source_path']
            with self.subTest(mode=mode,path='Windows separator identity'):
                self.assertTrue(validate_report(d,mode,'windows','1234567'))
        bad_paths=[('windows','D:\\owned\\test.native.json.source.png','E:/owned/test.native.json.source.png'),
                   ('windows','D:\\owned\\test.native.json.source.png','D:/other/test.native.json.source.png'),
                   ('windows','D:\\owned\\test.native.json.source.png','D:/owned/other.png'),
                   ('windows','D:\\owned\\test.native.json.source.png','D:owned/test.native.json.source.png'),
                   ('windows','D:\\owned\\test.native.json.source.png','D:/owned/sub/../test.native.json.source.png'),
                   ('windows','D:\\owned\\test.native.json.source.png','D:/Owned/test.native.json.source.png'),
                   ('cocoa','/owned/test.native.json.source.png','/owned\\test.native.json.source.png'),
                   ('xcb','/owned/test.native.json.source.png','/owned\\test.native.json.source.png'),
                   ('windows','D:\\owned\\test.native.json.source.png',None),
                   ('windows','D:\\owned\\test.native.json.source.png',17)]
        for qpa,source,actual in bad_paths:
            d=fixture('cpu',qpa);d['source_path']=source
            for row in d['pointers']:row['file']=source
            d['pointers'][0]['file']=actual
            with self.subTest(qpa=qpa,path=actual):
                with self.assertRaises((ValueError,TypeError,KeyError,IndexError)):
                    validate_report(d,'cpu',qpa,'1234567')

        for dpr in (1.,1.5,2.):
            for resize in (False,True):
                with self.subTest(dpr=dpr,resize=resize):
                    self.assertTrue(validate_window_evidence(window_fixture(dpr,resize)))
        window_edits=[(('window_capture_admission','accepted'),False),(('window_capture_admission','input_started'),True),
            (('window_capture_admission','phase'),1),(('window_capture_admission','resize_count'),2),(('window_capture_admission','resize_count'),True),
            (('window_capture_admission','logical_width'),True),(('window_capture_admission','physical_height'),1171),
            (('window_capture_admission','DPR'),2),(('window_capture_admission','initial_logical_size'),[1180,800]),
            (('window_capture_admission','first_observed_physical_size'),[1770,1169]),(('window_capture_admission','presented_frames'),2),
            (('window_capture_admission','source_key'),'0'),(('window_capture_admission','render_revision'),'0'),
            (('window_capture_admission','GUI_thread_id'),'62'),(('window_capture_admission','at_ms'),101),
            (('window_captures',0,'role'),'undo'),(('window_captures',0,'phase'),3),(('window_captures',0,'physical_height'),720),
            (('window_captures',0,'DPR'),1),(('window_captures',0,'image_bits_per_pixel'),64),
            (('window_captures',0,'path'),'/other.png'),(('window_captures',0,'source_key'),'999'),
            (('window_captures',0,'render_revision'),'999'),(('window_captures',0,'GUI_thread_id'),'62'),
            (('window_captures',0,'presented_frames'),2),(('window_captures',0,'at_ms'),299),
            (('window_captures',2,'at_ms'),651),(('window_captures',3,'at_ms'),674)]
        for path,value in window_edits:
            d=window_fixture();cursor=d
            for key in path[:-1]:cursor=cursor[key]
            cursor[path[-1]]=value
            with self.subTest(window_path=path,value=value):
                with self.assertRaises((ValueError,TypeError,KeyError,IndexError)):
                    validate_window_evidence(d)

        rounding=window_fixture(1.5)
        a=rounding['window_capture_admission'];a.update(initial_logical_size=[1183,721],first_observed_physical_size=[1775,1082],logical_width=1183,logical_height=721,physical_width=1775,physical_height=1082)
        for w in rounding['stable_presented_warmups']:w['signature'][2:4]=[1183,721]
        for c in rounding['window_captures']:c.update(logical_width=1183,logical_height=721,physical_width=1775,physical_height=1082)
        self.assertTrue(validate_window_evidence(rounding))
        wrong=deepcopy(rounding);wrong['window_capture_admission']['physical_width']=1774;wrong['window_capture_admission']['first_observed_physical_size'][0]=1774
        for c in wrong['window_captures']:c['physical_width']=1774
        with self.assertRaises((ValueError,TypeError,KeyError,IndexError)):validate_window_evidence(wrong)
        for target in ('admission','capture'):
            d=window_fixture(1.)
            if target=='admission':d['window_capture_admission']['DPR']=True
            else:d['window_captures'][0]['DPR']=True
            with self.assertRaises((ValueError,TypeError,KeyError,IndexError)):validate_window_evidence(d)

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
