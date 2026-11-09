"""Validate manual tangential controls through the complete normal photo App."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zipfile
from ValidateRawPressureQpa import png, file_sha, require

FALSE_SCOPE = ('physical_device_verified', 'native_float_RAW_decoder',
               'normal_Root_or_CI_accepted', 'product_development_complete')
DEVELOP_FIELDS = set(('exposure contrast temperature tint highlights shadows whites blacks saturation vibrance hue '
                      'highlightRecovery masterCurve redCurve greenCurve blueCurve hslHue hslSaturation hslLuminance look geometry').split())

def integer(value, minimum=0):
    return type(value) is int and value >= minimum

def finite(value):
    return type(value) in (int, float) and math.isfinite(value)

def positive_id(value):
    return isinstance(value,str) and re.fullmatch(r'[0-9]+',value) is not None and int(value)>0

def same_recorded_source(actual, expected, qpa):
    if not isinstance(actual, str) or not isinstance(expected, str) or not actual or not expected:
        return False
    if actual == expected:
        return True
    return (qpa == 'windows' and PureWindowsPath(actual).is_absolute()
            and PureWindowsPath(expected).is_absolute()
            and actual.replace('\\', '/') == expected.replace('\\', '/'))

def validate_report(data, mode, qpa, expected_commit=None):
    require(mode in ('cpu','gpu') and qpa in ('xcb','windows','cocoa'), 'mode/QPA')
    require(data['passed'] is True and data['error']=='' and type(data['phase']) is int and data['phase']==14, 'complete App')
    require(data['language']==('en_US' if mode=='cpu' else 'zh_CN') and data['actual_qpa']==qpa, 'language/QPA')
    require(all(data[k] is True for k in ('actual_Main_App_PhotoCanvas','controller_on_actual_GUI_thread','source_unchanged','settings_isolated')), 'actual Main App/source/GUI/settings')
    require(all(data[k] is False for k in FALSE_SCOPE), 'unsupported acceptance claim')
    require(data['GPU_full_readback_fingerprint_only_not_new_perpixel40proof'] is True, 'honest GPU MainApp scope')
    require(data['GPU_active'] is (mode=='gpu'), 'actual GPU selection')
    backend=data['actual_backend'];require(isinstance(backend,str) and backend, 'backend')
    if mode=='gpu':
        require({'xcb':'OpenGL','windows':'Direct3D 11','cocoa':'Metal'}[qpa].lower() in backend.lower(), 'actual GPU API')
    else:
        require('CPU' in backend, 'actual CPU backend')
    require(integer(data['elapsed_ms']) and data['elapsed_ms']<60000 and type(data['deadline_ms']) is int and data['deadline_ms']==60000, 'deadline')
    require(finite(data['DPR']) and data['DPR']>0, 'DPR')
    require(re.fullmatch(r'(?:[0-9a-f]{7,40}|unknown)',data['source_commit']) is not None, 'compile stamp')
    if expected_commit:
        require(re.fullmatch(r'[0-9a-f]{7,40}',expected_commit) is not None and data['source_commit'].startswith(expected_commit), 'expected commit')
    require(re.fullmatch(r'[0-9a-f]{64}',data['source_sha256']) is not None and bool(data['source_path']) and bool(data['project_path']), 'source/project')
    initial,first,active=(data[k]for k in ('initial_whole_Develop','first_whole_Develop','active_whole_Develop'))
    require(all(type(x) is dict and set(x)==DEVELOP_FIELDS for x in (initial,first,active)), 'complete whole Develop')
    require({k:v for k,v in initial.items()if k!='geometry'}=={k:v for k,v in first.items()if k!='geometry'}=={k:v for k,v in active.items()if k!='geometry'}, 'retained whole Develop fields')
    require(initial['geometry']=={'schema':1,'x':0,'y':0,'width':1,'height':1,'quarterTurns':0,'flipHorizontal':False,'flipVertical':False}, 'initial exact legacy geometry')
    g1,g2=first['geometry'],active['geometry']
    require(all(type(g[k]) is bool for g in (initial['geometry'],g1,g2) for k in ('flipHorizontal','flipVertical')), 'typed geometry flags')
    fields=set(initial['geometry'])|{'straighten','perspectiveHorizontal','perspectiveVertical','distortion','redCa','blueCa','tangentialP1','tangentialP2'}
    require(set(g1)==set(g2)==fields and type(g1['schema']) is int and type(g2['schema']) is int and g1['schema']==g2['schema']==4, 'active schema4')
    require(finite(g1['tangentialP1']) and .003<g1['tangentialP1']<=.02 and g1['tangentialP2']==0, 'first actual pointer')
    require(finite(g2['tangentialP2']) and -.02<=g2['tangentialP2']<-.003 and g2['tangentialP1']==g1['tangentialP1'], 'second actual pointer')
    require({k:v for k,v in g1.items()if k!='tangentialP2'}=={k:v for k,v in g2.items()if k!='tangentialP2'}, 'retained complete geometry')
    staging=data['staging'];count=staging['complete_pixels']
    require(integer(count,1) and integer(staging['corrected_width'],1) and integer(staging['corrected_height'],1)
            and count==staging['corrected_width']*staging['corrected_height'] and staging['corrected_width']<=307 and staging['corrected_height']<=211, 'complete geometry dimensions')
    require(staging['bit_exact'] is True and staging['actual_FP32_staging_sha256']==staging['expected_FP32_staging_sha256']
            and re.fullmatch(r'[0-9a-f]{64}',staging['actual_FP32_staging_sha256']) is not None
            and re.fullmatch(r'[0-9a-f]{64}',staging['ICC_sha256']) is not None, 'complete bit exact staging/ICC')
    scope=data['scope'];require(scope['pixels']==count and type(scope['bins']) is int and scope['bins']==1024 and scope['four_histograms_exact'] is True and scope['clipping_exact'] is True, 'full current scopes')
    for channel in ('red','green','blue','luma'):
        values=scope[channel];require(type(values) is list and len(values)==1024 and all(integer(v) for v in values) and sum(values)==count, 'four complete 1024 counts')
    plots=data['scope_plots'];require(type(plots) is list and len(plots)==3 and [p['mode']for p in plots]==['waveform','parade','vectorscope'], 'all three current plot requests')
    require(all(p['current'] is True and p['full_resolution'] is True and p['pixels']==str(count)for p in plots), 'full current plot request identity')
    require(finite(data['prepare_cache_hit_delta']) and data['prepare_cache_hit_delta']>0, 'actual cache reuse')
    phases=data['phase_checkpoints'];require(len(phases)==14 and all(type(p['phase']) is int for p in phases) and [p['phase']for p in phases]==list(range(14)), 'complete ordered history/durable checkpoints')
    times=[p['at_ms']for p in phases];require(all(integer(t) and t<=data['elapsed_ms']for t in times) and times==sorted(times), 'ordered checkpoint times')
    pointers=data['pointers'];require(len(pointers)==3 and [p['target']for p in pointers]==['tangentialP1','tangentialP2','resetGeometryCorrections'], 'three actual shared-action pointers')
    for p in pointers:
        require(p['QPA_mouse_route'] is True and positive_id(p['source_key']) and positive_id(p['render_revision'])
                and positive_id(p['post_event_render_revision']) and int(p['post_event_render_revision'])>=int(p['render_revision'])
                and positive_id(p['GUI_thread_id']) and same_recorded_source(p['file'],data['source_path'],qpa), 'pre-event actual target/source/revision/GUI owner')
        require(isinstance(p['post_event_source_key'],str) and p['post_event_source_key'].isdecimal(), 'honest post event source invalidation')
        require(all(finite(p[k])for k in ('local_x','local_y','global_x','global_y')) and integer(p['at_ms']) and p['at_ms']<data['elapsed_ms'], 'finite pointer coordinates/time')
    require(len({p['GUI_thread_id']for p in pointers})==1 and [p['at_ms']for p in pointers]==sorted(p['at_ms']for p in pointers), 'single GUI owner and ordered pointers')
    warmups=data['stable_presented_warmups'];require(warmups and {w['phase']for w in warmups}==set(range(14)), 'every phase actually presented')
    for w in warmups:
        sig=w['signature'];require(integer(w['presented_frames'],3) and len(sig)==6 and positive_id(sig[0]) and positive_id(sig[1])
            and all(finite(x) and x>0 for x in sig[2:]), 'actual stable frame/source/revision')
    diagnostic=data['diagnostic'];require(positive_id(diagnostic['source_key']) and positive_id(diagnostic['request_revision'])
        and re.fullmatch(r'[0-9a-f]{64}',diagnostic['sha256']) is not None, 'frozen diagnostic source/request')
    return True

def qt_round_positive(value):
    require(finite(value) and value > 0, 'positive actual window coordinate')
    return math.floor(value + .5)

def validate_window_evidence(data):
    admission=data['window_capture_admission'];dpr=data['DPR'];pointers=data['pointers']
    require(admission['accepted'] is True and admission['input_started'] is False
            and type(admission['phase']) is int and admission['phase']==0, 'window admitted before any input')
    require(finite(dpr) and dpr > 0 and finite(admission['DPR']) and admission['DPR']==dpr, 'same actual admitted DPR')
    require(all(integer(admission[k],1) for k in ('logical_width','logical_height','physical_width','physical_height')), 'typed positive window bounds')
    logical=[admission['logical_width'],admission['logical_height']];physical=[admission['physical_width'],admission['physical_height']]
    require(physical==[qt_round_positive(v*dpr) for v in logical] and physical[0]*physical[1]*4<=64*1024*1024, 'complete bounded actual window pixels')
    initial,observed=(admission[k]for k in ('initial_logical_size','first_observed_physical_size'))
    require(all(type(v) is list and len(v)==2 and all(integer(x,1)for x in v)for v in (initial,observed)), 'actual initial window and observed drawable')
    count=admission['resize_count'];require(type(count) is int and 0<=count<=1, 'at most one pre-input host fit')
    if count:
        initial_pixels=[qt_round_positive(v*dpr)for v in initial]
        require(all(a<=b for a,b in zip(observed,initial_pixels)) and observed!=initial_pixels
                and logical==[qt_round_positive(v/dpr)for v in observed] and physical==observed, 'one contained exact drawable fit')
    else:
        require(initial==logical and observed==physical, 'no hidden resize or substituted capture')
    require(integer(admission['presented_frames'],3) and positive_id(admission['source_key'])
            and positive_id(admission['render_revision']) and admission['GUI_thread_id']==pointers[0]['GUI_thread_id'], 'actual pre-input frames/source/revision/GUI owner')
    phases=data['phase_checkpoints'];require(len(phases)==14 and [p['phase']for p in phases]==list(range(14)), 'whole phase timeline for window capture')
    require(integer(admission['at_ms']) and phases[0]['at_ms']<=admission['at_ms']<=phases[1]['at_ms']
            and admission['at_ms']<=pointers[0]['at_ms'], 'admission precedes actual first pointer')
    captures=data['window_captures'];require(type(captures) is list and len(captures)==4, 'four complete actual window captures')
    for capture,role,phase in zip(captures,('active','undo','redo','reset'),(4,7,8,9)):
        require(capture['role']==role and type(capture['phase']) is int and capture['phase']==phase, 'ordered actual window capture phase')
        require(all(type(capture[k]) is int and capture[k]==admission[k]for k in ('logical_width','logical_height','physical_width','physical_height'))
                and finite(capture['DPR']) and capture['DPR']==dpr and type(capture['image_bits_per_pixel']) is int and capture['image_bits_per_pixel']==32, 'same complete admitted current surface')
        require(capture['path']==data['source_path'].removesuffix('.source.png')+'.'+role+'.png', 'owned actual capture path')
        warmup=[w for w in data['stable_presented_warmups']if w['phase']==phase][-1]
        require(capture['source_key']==warmup['signature'][0] and capture['render_revision']==warmup['signature'][1]
                and positive_id(capture['source_key']) and positive_id(capture['render_revision'])
                and capture['GUI_thread_id']==admission['GUI_thread_id'] and integer(capture['presented_frames'],3), 'capture belongs to actual stable phase/source/revision/GUI owner')
        require(integer(capture['at_ms']) and phases[phase]['at_ms']<=capture['at_ms']<=phases[phase+1]['at_ms']
                and capture['at_ms']<=data['elapsed_ms'], 'actual capture inside its phase')
    require(pointers[1]['at_ms']<=captures[0]['at_ms'] and captures[2]['at_ms']<=pointers[2]['at_ms']<=captures[3]['at_ms'], 'captures retain pointer/reset order')
    return True

def validate_tangential(binary, environment, home, report, mode, *, expected_commit=None):
    binary,home,report=Path(binary).resolve(),Path(home).resolve(),Path(report).resolve()
    require(mode in ('cpu','gpu') and binary.is_file(), 'binary/mode')
    require(not report.exists() and not report.is_symlink() and not list(report.parent.glob(report.stem+'.*')), 'fresh report namespace')
    report.parent.mkdir(parents=True,exist_ok=True);home.mkdir(parents=True,exist_ok=True);(home/'tmp').mkdir(exist_ok=True)
    env=environment.copy()
    for key in ('JIXELLIGHT_REQUIRE_GPU','JIXELLIGHT_FORCE_CPU','JIXELLIGHT_DISABLE_GPU','QSG_RHI_BACKEND','QSG_RHI_PREFER_SOFTWARE_RENDERER'):
        env.pop(key,None)
    for key in ('HOME','USERPROFILE','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','APPDATA','LOCALAPPDATA'):
        env[key]=str(home)
    env['TMPDIR']=str(home/'tmp');env['QML_DISABLE_DISK_CACHE']='1';env['JIXELLIGHT_CPU_THREADS']='2'
    qpa='windows' if sys.platform=='win32' else 'cocoa' if sys.platform=='darwin' else 'xcb';env['QT_QPA_PLATFORM']=qpa
    if mode=='cpu':env['JIXELLIGHT_FORCE_CPU']='1'
    else:
        env['JIXELLIGHT_REQUIRE_GPU']='1';env['QSG_RHI_BACKEND']={'windows':'d3d11','cocoa':'metal','xcb':'opengl'}[qpa]
        if qpa=='windows':env['QSG_RHI_PREFER_SOFTWARE_RENDERER']='1'
        if qpa=='xcb':env['LIBGL_ALWAYS_SOFTWARE']='1'
    native=report.with_name(report.stem+'.native.json');language='en_US' if mode=='cpu' else 'zh_CN'
    argv=[str(binary),'--tangential-geometry-smoke-report',str(native),'--tangential-geometry-language',language]
    if qpa=='xcb' and not env.get('DISPLAY'):
        xvfb=shutil.which('xvfb-run',path=env.get('PATH'));require(xvfb is not None,'Xvfb required for real xcb App')
        argv=[xvfb,'-a','-s','-screen 0 1600x1100x24',*argv]
    pref_paths=[Path.home()/'Library/Preferences'/name for name in ('com.jixellight.JixelLight.plist','JixelLight.JixelLight.plist','com.JixelLight.JixelLight.plist')]if sys.platform=='darwin'else[]
    preference_states=lambda:{str(p):{'exists':p.is_file(),'sha256':file_sha(p)if p.is_file()else None}for p in pref_paths}
    before=preference_states();before_path=report.with_name(report.name+'.preferences-before.json')
    before_path.write_text(json.dumps({'scope':'inherited Python user home before any App','states':before},indent=2)+'\n')
    binary_sha=file_sha(binary);report.with_name(report.name+'.invocation.json').write_text(json.dumps({'argv':argv,'binary_sha256':binary_sha,'qpa':qpa,'language':language,'force_CPU':mode=='cpu','require_GPU':mode=='gpu','preferences_before_sha256':file_sha(before_path)},indent=2)+'\n')
    result={'mode':'tangential-'+mode,'passed':False,'native_path':str(native),'GPU_readback_fingerprint_only':True,'physical_device_verified':False,'native_float_RAW_accepted':False,'normal_Root_CI_accepted':False}
    stdout=report.with_name(report.name+'.stdout');stderr=report.with_name(report.name+'.stderr');start=time.monotonic()
    try:
        with stdout.open('xb')as out,stderr.open('xb')as err:
            try:code=subprocess.run(argv,cwd=home,env=env,stdout=out,stderr=err,timeout=75).returncode
            except subprocess.TimeoutExpired:code=124
        result['returncode']=code;data=json.loads(native.read_text());result['report']=data;require(code==0,'App process exit');validate_report(data,mode,qpa,expected_commit);validate_window_evidence(data)
        text=stderr.read_text(errors='replace')+stdout.read_text(errors='replace');errors=[x for x in text.splitlines()if any(k in x for k in ('TypeError:','ReferenceError:','Unable to assign [undefined]'))];result['qml_runtime_errors']=errors;require(not errors,'QML runtime error')
        base=str(native);source=Path(base+'.source.png');require(data['source_path']==str(source) and not source.is_symlink(),'owned source')
        images={};pixels={}
        for role in ('source','geometry','color-reference','active','undo','redo','reset'):
            path=Path(base+'.'+role+'.png');require(not path.is_symlink(),'owned PNG');row,decoded=png(path);images[role]=row;pixels[role]=decoded
            if role=='source':require((row['width'],row['height'],row['depth'],row['color'])==(307,211,16,6) and row['ICC_bytes']>0 and row['file_sha256']==data['source_sha256'],'source PNG16/ICC/hash')
            elif role in ('geometry','color-reference'):
                require((row['width'],row['height'],row['depth'],row['color'])==(data['staging']['corrected_width'],data['staging']['corrected_height'],16,6) and row['ICC_bytes']>0,'full geometry/color PNG16/ICC')
                if role=='geometry':
                    require(row['ICC_sha256']==data['staging']['ICC_sha256'],'geometry ICC');values=bytearray()
                    for pixel in struct.iter_unpack('>4H',decoded):values.extend(struct.pack('<4f',*(v/65535.0 for v in pixel)))
                    require(hashlib.sha256(values).hexdigest()==data['staging']['actual_FP32_staging_sha256'],'every geometry RGBA16 to actual FP32 staging channel')
                else:require(decoded[6::8]==pixels['geometry'][6::8] and decoded[7::8]==pixels['geometry'][7::8],'every exact geometry alpha')
            else:
                phase={'active':4,'undo':7,'redo':8,'reset':9}[role];warmup=[w for w in data['stable_presented_warmups']if w['phase']==phase][-1];sig=warmup['signature']
                require(row['width']==qt_round_positive(sig[2]*data['DPR']) and row['height']==qt_round_positive(sig[3]*data['DPR']) and row['depth']==8,'actual complete presented window/DPR')
            with Path(base+'.'+role+'-byte-proof.json').open('x')as f:json.dump(row,f,indent=2);f.write('\n')
        diagnostic=Path(base+'.diagnostic.zip');require(data['diagnostic']['path']==str(diagnostic) and not diagnostic.is_symlink() and file_sha(diagnostic)==data['diagnostic']['sha256'],'frozen original ZIP')
        with zipfile.ZipFile(diagnostic)as archive:
            info=archive.getinfo('current_preview.png');require(info.file_size<32*1024*1024,'bounded original ZIP PNG');payload=archive.read(info)
        path=Path(base+'.original-diagnostic-preview.png')
        with path.open('xb')as f:f.write(payload)
        row,decoded=png(path);require(decoded==pixels['color-reference'] and row['ICC_sha256']==images['color-reference']['ICC_sha256'],'entire frozen CPU reference PNG and ICC')
        images['original-diagnostic-preview']=row
        with Path(base+'.original-diagnostic-preview-byte-proof.json').open('x')as f:json.dump(row,f,indent=2);f.write('\n')
        require(before==preference_states() and file_sha(binary)==binary_sha and file_sha(source)==data['source_sha256'],'preferences/binary/source unchanged')
        result.update(passed=True,source_commit=data['source_commit'],backend=data['actual_backend'],language=language,complete_staging_pixels=data['staging']['complete_pixels'],first_PNG_byte_proofs=images,first_full_unfiltered_rows=sum(r['full_unfiltered_rows']for r in images.values()),preference_states_unchanged=True,native_report_sha256=file_sha(native),all_output_files={p.name:file_sha(p)for p in report.parent.glob(native.name+'*')if p.is_file()})
    except (OSError,ValueError,TypeError,KeyError,IndexError,AssertionError,struct.error,zipfile.BadZipFile)as error:
        result['error']=str(error)
    result.update(elapsed_ms=round((time.monotonic()-start)*1000),stdout_sha256=file_sha(stdout)if stdout.is_file()else None,stderr_sha256=file_sha(stderr)if stderr.is_file()else None)
    with report.open('x')as f:json.dump(result,f,indent=2);f.write('\n')
    return result

def main():
    parser=argparse.ArgumentParser();parser.add_argument('executable',type=Path);parser.add_argument('report',type=Path);parser.add_argument('mode',choices=('cpu','gpu'));parser.add_argument('--expected-commit');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='jixellight-tangential-')as home:
        result=validate_tangential(args.executable,os.environ.copy(),Path(home),args.report,args.mode,expected_commit=args.expected_commit)
    print(json.dumps({'mode':result['mode'],'passed':result['passed'],'source_commit':result.get('source_commit'),'error':result.get('error')}),flush=True)
    return 0 if result['passed']else 1

if __name__=='__main__':raise SystemExit(main())
