"""Validate luminanceMask controls through the complete normal photo App."""
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
    require(data['passed'] is True and data['error']=='' and type(data['phase']) is int and data['phase']==16, 'complete luminanceMask App workflow')
    require(data['language']==('en_US' if mode=='cpu' else 'zh_CN') and data['actual_qpa']==qpa, 'actual language/QPA')
    require(all(data[k] is True for k in ('actual_Main_App_PhotoCanvas','controller_on_actual_GUI_thread','source_unchanged','settings_isolated')), 'actual Main App/source/GUI/settings')
    require(all(data[k] is False for k in FALSE_SCOPE), 'unsupported acceptance claim')
    require(data['GPU_full_readback_fingerprint_only_not_new_perpixel40proof'] is True, 'honest GPU readback scope')
    require(data['GPU_active'] is (mode=='gpu'), 'actual processor selection')
    backend=data['actual_backend'];require(isinstance(backend,str) and backend, 'backend')
    require(({'xcb':'OpenGL','windows':'Direct3D 11','cocoa':'Metal'}[qpa] if mode=='gpu' else 'CPU').lower() in backend.lower(), 'actual processing API')
    require(integer(data['elapsed_ms']) and data['elapsed_ms']<60000
            and type(data['deadline_ms']) is int and data['deadline_ms']==60000, 'unchanged operation deadline')
    require(re.fullmatch(r'[0-9a-f]{7,40}',data['source_commit']) is not None, 'compile stamp')
    if expected_commit:require(re.fullmatch(r'[0-9a-f]{7,40}',expected_commit) is not None
            and (data['source_commit']==expected_commit or expected_commit.startswith(data['source_commit'])), 'expected actual build stamp')
    initial,first,active,reset=(data[k] for k in ('initial_whole_Develop','first_whole_Develop','active_whole_Develop','reset_whole_Develop'))
    require(all(type(v) is dict for v in (initial,first,active,reset))
            and set(initial)==set(first)==set(active)==DEVELOP_FIELDS|{'grain','denoise','luminanceMask'}
            and set(reset)==DEVELOP_FIELDS|{'grain','denoise'}, 'whole Develop snapshots')
    require(initial['geometry']=={'schema':1,'x':0,'y':0,'width':1,'height':1,'quarterTurns':0,
            'flipHorizontal':False,'flipVertical':False}, 'unchanged identity geometry')
    require(initial['denoise']=={'schema':1,'lightness':25,'chroma':35}, 'independent input denoise')
    for selection in (initial['luminanceMask'],first['luminanceMask'],active['luminanceMask']):
        require(type(selection) is dict and set(selection)=={'schema','enabled','inverted','exposure','lower','upper','feather'}
                and type(selection['schema']) is int and selection['schema']==1 and selection['enabled'] is True
                and type(selection['inverted']) is bool and finite(selection['exposure']) and -3<=selection['exposure']<=3
                and all(finite(selection[k]) for k in ('lower','upper','feather'))
                and 0<selection['lower']<=selection['upper']<.6 and .001<=selection['feather']<.1, 'canonical bounded luminance selection')
        require(all(selection[k]==initial['luminanceMask'][k] for k in ('lower','upper','feather')), 'range survives exposure edits')
    require(initial['luminanceMask']['exposure']==0 and first['luminanceMask']['exposure']>.5 and active['luminanceMask']['exposure']<-.5
            and initial['luminanceMask']['inverted'] is False and first['luminanceMask']['inverted'] is False
            and active['luminanceMask']['inverted'] is True, 'signed real slider and invert edits')
    initial_other={k:v for k,v in initial.items() if k!='luminanceMask'}
    first_other={k:v for k,v in first.items() if k!='luminanceMask'};active_other={k:v for k,v in active.items() if k!='luminanceMask'}
    require(first_other==initial_other and active_other==dict(initial_other,exposure=.3) and reset==active_other
            and active['grain']==initial['grain'] and active['grain']['amount']==17, 'all unrelated edits preserved')
    require(data['undo_steps_to_first']==3 and finite(data['luminanceMask_cache_hit_delta'])
            and data['luminanceMask_cache_hit_delta']>=(1 if mode=='cpu' else 0), 'actual cache policy and undo granularity')
    require(data['luminanceMask_cache_required'] is (mode=='cpu')
            and data['luminanceMask_processing_policy']==('cached-cpu-reference' if mode=='cpu' else 'uncached-cpu-reference-upload'), 'honest complete CPU policy')
    require(('LUMINANCE MASK' if mode=='cpu' else '亮度范围') in data['luminanceMask_header_text']
            and data['luminanceMask_reset_text']==('Reset' if mode=='cpu' else '重置'), 'actual translated controls')
    staging=data['staging'];require(staging['bit_exact'] is True and staging['complete_pixels']==307*211 and staging['actual_FP32_staging_sha256']==staging['expected_FP32_staging_sha256'], 'complete unchanged staging')
    scope=data['scope'];require(scope['pixels']==307*211 and scope['bins']==1024 and scope['four_histograms_exact'] is True and scope['clipping_exact'] is True, 'full exact scopes')
    for key in ('red','green','blue','luma'):require(len(scope[key])==1024 and all(integer(x) for x in scope[key]) and sum(scope[key])==scope['pixels'], 'all exact histogram bins')
    require([p['mode'] for p in data['scope_plots']]==['waveform','parade','vectorscope'] and all(p['current'] and p['full_resolution'] and int(p['pixels'])==scope['pixels'] for p in data['scope_plots']), 'three full plots')
    require([p['target'] for p in data['pointers']]==['enableMask','lowerRange','upperRange','featherRange','positiveExposure','negativeExposure','invertRange','luminanceMaskReset'], 'eight actual QPA toggle/range/exposure/invert/reset events')
    for index,p in enumerate(data['pointers']):
        require(p['QPA_mouse_route'] is True and positive_id(p['source_key']) and positive_id(p['render_revision'])
                and positive_id(p['post_event_render_revision']) and positive_id(p['GUI_thread_id'])
                and all(finite(p[k]) for k in ('local_x','local_y','global_x','global_y'))
                and p['source_key']==p['post_event_source_key'] and same_recorded_source(p['file'],data['source_path'],qpa), 'actual QPA controls on stable source')
        if index>=0:require(int(p['post_event_render_revision'])>int(p['render_revision']), 'slider/reset applied synchronously')
    require(data['export']['RGBA16_exact'] is True and data['export']['ICC_srgb'] is True and data['export']['pixels']==scope['pixels'], 'actual exact ICC export')
    return True

def qt_round_positive(value):
    require(finite(value) and value > 0, 'positive actual window coordinate')
    return math.floor(value + .5)

def validate_diagnostic(data, manifest, stages, mode):
    require(manifest['adjustments']==data['active_whole_Develop'], 'frozen whole active Develop')
    require(str(stages['parameter_revision'])==data['diagnostic']['request_revision']
            and stages['gpu_context']['source_cache_key']==data['diagnostic']['source_key'], 'frozen diagnostic revision and source')
    color=stages['color_stages'];mask=color['luminance_mask'];denoise=color['spatial_denoise']
    require(color['available'] is True and color['backend']=='cpu-reference' and mask['backend']=='cpu-reference'
            and mask['enabled'] is True and mask['active'] is True and mask['monitor_icc'] is False
            and mask['output_boundary']=='Vignette' and denoise['backend']=='cpu-reference', 'frozen luminance mask and input denoise')
    for field,reported in (('lower','lower_y'),('upper','upper_y'),('feather','feather_y'),('exposure','exposure_ev'),('inverted','inverted')):
        require(mask[reported]==data['active_whole_Develop']['luminanceMask'][field], 'frozen mask range/exposure/inversion')
    require([v['id'] for v in color['entries']]==['input_linear','wb_exposure','vignette','highlight_recovery','tone_neutral','perceptual_look','output_linear','output_transfer','look_lut','grain']
            and all(v['pixels']==307*211 and v['non_finite_values']==0 for v in color['entries']), 'complete finite original working boundaries')
    gpu=stages['gpu_working_output']
    if mode=='gpu':
        require(gpu['available'] is True and gpu['producer']=='cpu-reference-upload'
                and gpu['luminance_mask']=='cpu-reference' and gpu['spatial_denoise']=='cpu-reference'
                and gpu['numeric_source_policy']=='luminance-mask-cpu-stage' and gpu['monitor_icc'] is False
                and gpu['pixels']==307*211 and gpu['bytes']==307*211*16 and gpu['nonfinite_rgba']==[0,0,0,0]
                and str(gpu['parameter_revision'])==data['diagnostic']['request_revision']
                and gpu['source_cache_key']==data['diagnostic']['source_key'], 'actual complete CPU upload on Metal/D3D/OpenGL')
    else:require(gpu['available'] is False, 'CPU workflow does not claim GPU output')
    return True

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
    phases=data['phase_checkpoints'];require(len(phases)==16 and [p['phase']for p in phases]==list(range(16)), 'whole phase timeline for window capture')
    require(all(type(p['phase']) is int and integer(p['at_ms']) and p['at_ms']<=data['elapsed_ms'] for p in phases)
            and [p['at_ms'] for p in phases]==sorted(p['at_ms'] for p in phases), 'ordered complete phase times')
    warmups=data['stable_presented_warmups']
    require({w['phase'] for w in warmups}==set(range(16)), 'every phase actually presented')
    for warmup in warmups:
        signature=warmup['signature']
        require(integer(warmup['presented_frames'],3) and len(signature)==6
                and positive_id(signature[0]) and positive_id(signature[1])
                and all(finite(x) and x>0 for x in signature[2:]), 'complete stable source/revision/window/preview')
    require(len({p['GUI_thread_id'] for p in pointers})==1
            and all(integer(p['at_ms']) and p['at_ms']<data['elapsed_ms'] for p in pointers)
            and [p['at_ms'] for p in pointers]==sorted(p['at_ms'] for p in pointers), 'single GUI owner and ordered pointers')
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
    require(pointers[6]['at_ms']<=captures[0]['at_ms'] and captures[2]['at_ms']<=pointers[7]['at_ms']<=captures[3]['at_ms'], 'captures retain pointer/reset order')
    return True

def validate_luminance_mask(binary, environment, home, report, mode, *, expected_commit=None):
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
    argv=[str(binary),'--luminance-mask-smoke-report',str(native),'--luminance-mask-language',language]
    if qpa=='xcb' and not env.get('DISPLAY'):
        xvfb=shutil.which('xvfb-run',path=env.get('PATH'));require(xvfb is not None,'Xvfb required for real xcb App')
        argv=[xvfb,'-a','-s','-screen 0 1600x1100x24',*argv]
    pref_paths=[Path.home()/'Library/Preferences'/name for name in ('com.jixellight.JixelLight.plist','JixelLight.JixelLight.plist','com.JixelLight.JixelLight.plist')]if sys.platform=='darwin'else[]
    preference_states=lambda:{str(p):{'exists':p.is_file(),'sha256':file_sha(p)if p.is_file()else None}for p in pref_paths}
    before=preference_states();before_path=report.with_name(report.name+'.preferences-before.json')
    before_path.write_text(json.dumps({'scope':'inherited Python user home before any App','states':before},indent=2)+'\n')
    binary_sha=file_sha(binary);report.with_name(report.name+'.invocation.json').write_text(json.dumps({'argv':argv,'binary_sha256':binary_sha,'qpa':qpa,'language':language,'force_CPU':mode=='cpu','require_GPU':mode=='gpu','preferences_before_sha256':file_sha(before_path)},indent=2)+'\n')
    result={'mode':'luminanceMask-'+mode,'passed':False,'native_path':str(native),'GPU_readback_fingerprint_only':True,'physical_device_verified':False,'native_float_RAW_accepted':False,'normal_Root_CI_accepted':False}
    stdout=report.with_name(report.name+'.stdout');stderr=report.with_name(report.name+'.stderr');start=time.monotonic()
    try:
        with stdout.open('xb')as out,stderr.open('xb')as err:
            try:code=subprocess.run(argv,cwd=home,env=env,stdout=out,stderr=err,timeout=75).returncode
            except subprocess.TimeoutExpired:code=124
        result['returncode']=code;data=json.loads(native.read_text());result['report']=data;require(code==0,'App process exit');validate_report(data,mode,qpa,expected_commit);validate_window_evidence(data)
        text=stderr.read_text(errors='replace')+stdout.read_text(errors='replace');errors=[x for x in text.splitlines()if any(k in x for k in ('TypeError:','ReferenceError:','Unable to assign [undefined]'))];result['qml_runtime_errors']=errors;require(not errors,'QML runtime error')
        base=str(native);source=Path(base+'.source.png');require(data['source_path']==str(source) and not source.is_symlink(),'owned source')
        images={};pixels={}
        for role in ('source','geometry','color-reference','active','undo','redo','reset','export'):
            path=Path(base+'.'+role+'.png');require(not path.is_symlink(),'owned PNG');row,decoded=png(path);images[role]=row;pixels[role]=decoded
            if role=='source':require((row['width'],row['height'],row['depth'],row['color'])==(307,211,16,6) and row['ICC_bytes']>0 and row['file_sha256']==data['source_sha256'],'source PNG16/ICC/hash')
            elif role in ('geometry','color-reference','export'):
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
        require(pixels['export']==pixels['color-reference'] and images['export']['ICC_sha256']==images['color-reference']['ICC_sha256'] and images['export']['file_sha256']==data['export']['sha256'], 'every exported RGBA16 and ICC byte')
        diagnostic=Path(base+'.diagnostic.zip');require(data['diagnostic']['path']==str(diagnostic) and not diagnostic.is_symlink() and file_sha(diagnostic)==data['diagnostic']['sha256'],'frozen original ZIP')
        with zipfile.ZipFile(diagnostic)as archive:
            manifest=json.loads(archive.read('manifest.json'));stages=json.loads(archive.read('stage_outputs.json'))
            validate_diagnostic(data,manifest,stages,mode)
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
    with tempfile.TemporaryDirectory(prefix='jixellight-luminanceMask-')as home:
        result=validate_luminance_mask(args.executable,os.environ.copy(),Path(home),args.report,args.mode,expected_commit=args.expected_commit)
    print(json.dumps({'mode':result['mode'],'passed':result['passed'],'source_commit':result.get('source_commit'),'error':result.get('error')}),flush=True)
    return 0 if result['passed']else 1

if __name__=='__main__':raise SystemExit(main())
