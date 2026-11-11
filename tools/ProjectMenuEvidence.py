"""Strict owned menu-layout transport; schema validation is not platform acceptance."""
from __future__ import annotations
import json,math,re
from NativeBrushEvidence import commit

CASES=(('en_US',1180,720),('zh_CN',1180,720),('en_US',1540,920),('zh_CN',1540,920))
TEXT={'en_US':'Import XMP into current version…','zh_CN':'导入 XMP 到当前版本…'}
# Values taken from the pinned Qt6.8.3 GraphicsApi header, not inferred from OS names.
BACKENDS={'xcb':('OpenGL',3),'windows':('Direct3D 11',4),'cocoa':('Metal',6)}

def need(value,message):
    if not value:raise ValueError(message)

def integer(value,low,high=None):
    need(type(value)is int and low<=value<=(low if high is None else high),'typed integer')
    return value

def real(value,low=-2**31,high=2**31):
    need(type(value)in(int,float)and math.isfinite(value)and low<=value<=high,'typed finite real')
    return value

def decode_report(text):
    def pairs(rows):
        out={}
        for key,value in rows:
            need(key not in out,'duplicate menu JSON key');out[key]=value
        return out
    def floating(text):
        value=float(text);need(math.isfinite(value),'nonfinite menu JSON number');return value
    return json.loads(text,object_pairs_hook=pairs,parse_float=floating,parse_constant=lambda value:(_ for _ in()).throw(ValueError('nonfinite menu JSON')))

def validate(data,*,expected_commit=None):
    need(type(data)is dict and data['passed']is True and data['error']=='','successful owned menu report')
    integer(data['schema'],2);need(data['mode']=='project-menu-layout-only-v2','menu-only v2 provenance')
    commit(data['source_commit'],expected_commit)
    need(data['qt_version']=='6.8.3','pinned actual Qt version')
    integer(data['GUI_deadline_ms'],60000);integer(data['GUI_elapsed_ms'],1,59999)
    qpa=data['actual_qpa'];need(qpa in BACKENDS,'actual native App QPA')
    backend,enum=BACKENDS[qpa];need(data['actual_backend_name']==backend,'actual named renderer')
    integer(data['actual_backend'],enum);real(data['actual_DPR'],.25,8)
    roles=[data['GUI_thread'],data['actual_RHI_thread']]
    need(all(type(v)is str and re.fullmatch('[0-9a-f]+',v)and int(v,16)>0 for v in roles)and roles[0]!=roles[1],'distinct actual GUI and RHI roles')
    integer(data['actual_afterFrameEnd_count'],1,2**53)
    for key in ['actual_window_system_pointer','menu_close_is_controlled_cleanup','settings_isolated']:need(data[key]is True,'actual menu scope '+key)
    for key in ['image_import_or_XMP_merge_exercised','physical_input_or_monitor_calibration_claim','Root_or_CI_or_F0_F6_acceptance_claim']:need(data[key]is False,'no scope overclaim '+key)
    need(type(data['cases'])is list and len(data['cases'])==4,'all four ordered bilingual window cases')
    captures=[]
    for row,(language,width,height)in zip(data['cases'],CASES):
        need(type(row)is dict and row['language']==language and row['menu_text']==TEXT[language],'complete localized label')
        integer(row['window_width'],width);integer(row['window_height'],height)
        menu=real(row['actual_menu_width'],200,width-24)
        label=real(row['label_width'],1,menu);implicit=real(row['label_implicit_width'],1,menu);font=real(row['actual_font_text_width'],1,menu)
        need(row['actual_truncated']is False and label+1>=implicit and label+1>=font,'actual label is not elided or clipped')
        for key in ['actual_button_parent','anchor_error_at_most2','two_distinct_button_pointer_origins_stable']:need(row[key]is True,'actual measured menu condition '+key)
        need(real(row['first_pointer_fraction'],0,1)==.25 and real(row['second_pointer_fraction'],0,1)==.75,'two different actual pointer positions')
        for axis in ['x','y']:
            first=real(row['first_menu_global_'+axis]);second=real(row['menu_global_'+axis])
            need(first==second,'independently identical first/second menu origin')
            need(abs(first-real(row['first_button_bottom_global_'+axis]))<=2 and abs(second-real(row['button_bottom_global_'+axis]))<=2,'independent first and second anchor arithmetic')
        cap=row['capture'];need(type(cap)is dict and type(cap['path'])is str and cap['path'],'actual capture locator')
        integer(cap['width'],1,32768);integer(cap['height'],1,32768)
        need(type(cap['sha256'])is str and re.fullmatch('[0-9a-f]{64}',cap['sha256']),'actual capture SHA256')
        captures.append(dict(cap))
    return {'passed':True,'schema':2,'mode':'project-menu-layout','case_count':4,'languages':['en_US','zh_CN'],'logical_sizes':[[1180,720],[1540,920]],'actual_qpa':qpa,'actual_backend':backend,'source_commit':data['source_commit'],'independent_two_pointer_anchor_arithmetic':True,'physical_input_or_complete_UI_claim':False,'captures':captures}
