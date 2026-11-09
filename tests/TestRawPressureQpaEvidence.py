"""Synthetic metadata controls only; no App, sampler, pixel or PNG execution."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ValidateRawPressureQpa import FALSE_SCOPE, validate_report, validate_trace, empty_brush_document

def report(mode='cpu',qpa='cocoa'):
    data={'passed':True,'error':'','language':'en_US'if mode=='cpu'else'zh_CN','actual_qpa':qpa,
          'GPU_active':mode=='gpu','settings_isolated':True,'actual_photo_app_UI':True,
          'controlled_qpa_tablet':True,'source_unchanged':True,'elapsed_ms':1000,'deadline_ms':60000,
          'GUI_ticks':10,'maximum_send_ms':.5,'dpr':1,'source_commit':'a'*40,
          'actual_backend':'CPU parallel'if mode=='cpu'else{'cocoa':'GPU · Metal · Test','windows':'GPU · Direct3D 11 · Test','xcb':'GPU · OpenGL · Test'}[qpa]}
    data.update({k:False for k in FALSE_SCOPE})
    data['rounds']=[{'passed':True,'round':i,'mode':mode,'held_packets':7003,'sampler_dabs':2840,
                     'complete_RGBA64_pixels':64777,'pixel_sha256':'b'*64,'original_pixel_sha256':'b'*64,
                     'Undo_Redo_and_persistence_bit_exact':True,'source_unchanged':True,
                     'physical_or_arbitrary_jitter_claim':False}for i,mode in enumerate(('paint','erase'))]
    return data

def raw_trace(index=0):
    pressure=(.25,.875,.375,1.,.125,.75,.5,0.)
    return {'passed':True,'round':index,'source_file_sha256':'c'*64,'held_packets':7003,
            'unique_delivered_positions':7003,'original_sampler_dabs':2840,'release_pressure':0,
            'default_stabilizer_off':True,'single_history_commit':True,'first_published_sample':'1',
            'final_published_sample':'7003','sample1_published_before_final':True,'held_GUI_ticks':10,
            'physical_or_arbitrary_jitter_claim':False,'reference_uses_public_delivered_QML_source_mapping':True,
            'gesture':str(index+1),'epoch':'1','source_key':'9','catalog':'0','photo_key':'owned-fixture',
            'trace':[[i,.4+i*1e-8,.5+(i%17)*1e-9,.125 if i==7002 else pressure[i%8],
                      str(2002+index*14008+i*2)]for i in range(7003)]}

class TestRawPressureQpaEvidence(unittest.TestCase):
    def test_complete_two_round_metadata_all_platforms(self):
        for qpa in ('cocoa','windows','xcb'):
            for mode in ('cpu','gpu'):
                self.assertTrue(validate_report(report(mode,qpa),mode,qpa,'a'*7))

    def test_report_scope_and_backend_refusals(self):
        for value in ({},{'schema':1,'layers':[]},{'schema':2,'layers':[]}):
            self.assertTrue(empty_brush_document(value))
        for value in (None,[],False,{'layers':[]},{'schema':3,'layers':[]},
                      {'schema':True,'layers':[]},{'schema':'1','layers':[]},
                      {'schema':1,'layers':{}},{'schema':1,'layers':[{}]},
                      {'schema':1,'layers':[],'extra':0}):
            self.assertFalse(empty_brush_document(value))
        cases=[('passed',False),('language','zh_CN'),('actual_qpa','offscreen'),('GPU_active',True),
               ('settings_isolated',False),('actual_photo_app_UI',False),('source_unchanged',False),
               ('elapsed_ms',60000),('deadline_ms',120000),('GUI_ticks',True),('dpr',True),
               ('source_commit','future'),('actual_backend','GPU · Metal')]
        cases.extend((k,True)for k in FALSE_SCOPE)
        for key,value in cases:
            d=report();d[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):validate_report(d,'cpu','cocoa')
        d=report();d['rounds']=d['rounds'][:1]
        with self.assertRaises(ValueError):validate_report(d,'cpu','cocoa')
        with self.assertRaises(ValueError):validate_report(report(),'cpu','cocoa','d'*7)
        d=report('gpu');d['actual_backend']='GPU · Vulkan'
        with self.assertRaises(ValueError):validate_report(d,'gpu','cocoa')

    def test_full_synthetic_ordered_trace_both_rounds(self):
        for index in (0,1):self.assertEqual(len(validate_trace(raw_trace(index),index,'c'*64)),64)

    def test_trace_pressure_order_uniqueness_and_publication_refusals(self):
        cases=[(['held_packets'],7002),(['default_stabilizer_off'],False),(['single_history_commit'],False),
               (['first_published_sample'],'2'),(['final_published_sample'],'7002'),(['held_GUI_ticks'],True),
               (['source_key'],'0'),(['release_pressure'],1),(['physical_or_arbitrary_jitter_claim'],True),
               (['trace',3,3],True),(['trace',18,3],.13),(['trace',2,0],True),(['trace',2,4],'2000')]
        base=raw_trace()
        for path,value in cases:
            d=copy.deepcopy(base);target=d
            for k in path[:-1]:target=target[k]
            target[path[-1]]=value
            with self.subTest(path=path),self.assertRaises(ValueError):validate_trace(d,0,'c'*64)
        d=copy.deepcopy(base);d['trace'][1][1:3]=d['trace'][0][1:3]
        with self.assertRaises(ValueError):validate_trace(d,0,'c'*64)
        d=copy.deepcopy(base);d['trace'].pop()
        with self.assertRaises(ValueError):validate_trace(d,0,'c'*64)

if __name__=='__main__':unittest.main()
