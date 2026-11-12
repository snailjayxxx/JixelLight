"""Independent original-pixel, presentation and durable-state evidence for soft proof."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zipfile
from ValidateRawPressureQpa import png, file_sha, require
from ValidateColorMask import integer, finite, positive_id, same_recorded_source, qt_round_positive, DEVELOP_FIELDS


def validate_report(data, mode, qpa, expected_commit=None):
    require(mode in ('cpu','gpu') and qpa in ('cocoa','windows','xcb'), 'actual mode/QPA')
    require(data['passed'] is True and data['error']=='' and type(data['phase']) is int and data['phase']==15, 'complete proof workflow')
    require(data['language']==('en_US'if mode=='cpu'else'zh_CN') and data['actual_qpa']==qpa, 'native language/QPA')
    require(data['GPU_active'] is (mode=='gpu'), 'actual original-image processor')
    require(({'cocoa':'Metal','windows':'Direct3D 11','xcb':'OpenGL'}[qpa]if mode=='gpu'else'CPU').lower() in data['actual_backend'].lower(), 'actual original processing API')
    require(integer(data['elapsed_ms']) and data['elapsed_ms']<60000 and type(data['deadline_ms']) is int and data['deadline_ms']==60000, 'unchanged operation deadline')
    require(re.fullmatch(r'[0-9a-f]{7,40}',data['source_commit']) is not None, 'compile stamp')
    if expected_commit:require(re.fullmatch(r'[0-9a-f]{7,40}',expected_commit) is not None and expected_commit.startswith(data['source_commit']), 'exact expected build')
    for key in ('actual_Main_App_PhotoCanvas','controller_on_actual_GUI_thread','settings_isolated','source_unchanged','presentation_only','proof_independent_CPU_reference','exact_scopes_preserved','durable_edit_history_preserved','original_GPU_parity_covered_by_separate_suite'):
        require(data[key] is True, 'original App/edit/source evidence: '+key)
    for key in ('physical_device_verified','real_printer_profile_verified','native_float_RAW_decoder','product_development_complete'):
        require(data[key] is False, 'unsupported acceptance claim: '+key)
    require(type(data['initial_whole_Develop']) is dict and set(data['initial_whole_Develop'])==DEVELOP_FIELDS and data['initial_whole_Develop']==data['final_whole_Develop']
            and not any('proof'in k.lower()for k in data['initial_whole_Develop']), 'proof never becomes a Develop edit')
    require(type(data['initial_history']) is list and len(data['initial_history'])>=1 and data['initial_history']==data['final_history'], 'whole durable history unchanged')
    require(data['profile_model']=='RGB' and re.fullmatch(r'[0-9a-f]{64}',data['profile_sha256']) is not None, 'owned bounded output RGB ICC')
    require(('SOFT PROOF'if mode=='cpu'else'软打样') in data['header_text'], 'actual translated panel')
    scope=data['scope'];require(scope['pixels']==307*211 and scope['bins']==1024, 'complete original scope size')
    for key in ('red','green','blue','luma'):
        require(len(scope[key])==1024 and all(integer(v)for v in scope[key]) and sum(scope[key])==307*211, 'every original histogram bin')
    pointers=data['pointers'];require([p['target']for p in pointers]==['softProofPanelToggle','softProofEnable','softProofPaper','softProofGamut','softProofBlackPoint','softProofRefresh','softProofEnable','softProofEnable'], 'eight actual QPA control events')
    source,revision=pointers[0]['source_key'],pointers[0]['render_revision'];require(positive_id(source)and positive_id(revision), 'admitted original source/revision')
    previous=-1
    for p in pointers:
        require(p['QPA_mouse_route'] is True and p['source_key']==source and p['render_revision']==p['post_event_render_revision']==revision
                and finite(p['x']) and finite(p['y']) and integer(p['at_ms']) and previous<=p['at_ms']<=data['elapsed_ms'], 'real ordered presentation-only QPA input')
        previous=p['at_ms']
    proofs=data['proofs'];require([p['role']for p in proofs]==['relative','paper','gamut','black-point-off','monitor'], 'all five original proof presentations')
    prepared=None
    for index,p in enumerate(proofs):
        require(p['RGBA16_exact'] is True and p['actual_pixels_sha256']==p['expected_pixels_sha256']
                and re.fullmatch(r'[0-9a-f]{64}',p['actual_pixels_sha256']) is not None, 'all proof pixels match independent wide-input reference')
        f=p['facts'];require(f['enabled'] is True and f['available'] is True and f['state']=='ready' and f['error']=='' and f['target']=='custom'
                and f['presentation_only'] is True and f['alpha_exact'] is True and f['export_or_scopes_modified'] is False and f['monitor_icc_applied'] is False
                and f['input']=='gamma1.8-prophoto-rgba16' and f['output']=='encoded-srgb-rgba16' and f['proof_profile_model']=='RGB'
                and f['proof_profile_key']==f['profile_key']=='soft-proof-icc-v1:'+data['profile_sha256'], 'private CPU proof transform and immutable ICC')
        require(f['proof_width']==307 and f['proof_height']==211 and f['pixels']==307*211 and f['source_scaled_for_proof'] is False
                and f['presentation_reserved_bytes']==307*211*32 and integer(f['cpu_operation_reserved_bytes'],307*211*8), 'bounded admitted proof memory')
        require(f['gpu_staging_source_key']==source and f['parameter_revision']==revision and positive_id(f['prepared_source_key']) and positive_id(f['generation']), 'both immutable CPU and GPU source identities')
        if prepared is None:prepared=f['prepared_source_key']
        require(f['prepared_source_key']==prepared and f['intent']==1 and f['simulate_paper'] is (index>=1)
                and f['gamut_warning'] is (index>=2) and f['black_point_compensation'] is (index<3), 'actual selected proof modes and source reuse')
        require(f['monitor_lut_key']==('owned-proof-invert'if index==4 else'identity-srgb'), 'monitor applied only in existing provider presentation')
    require(len({p['actual_pixels_sha256']for p in proofs[:3]})==3 and proofs[3]['actual_pixels_sha256']!=proofs[4]['actual_pixels_sha256'], 'paper/gamut/monitor visibly change owned pixels')
    live=data['live_invariants']
    require(all(live[k]is True for k in ('source_same','revision_same','state_same','history_same','red_same','green_same','blue_same','luma_same','shadow_same','highlight_same'))
            and live['source_key']==data['source_key']and live['revision']==data['render_revision']and live['pixels']==307*211, 'final live source/revision/edits/scopes remain frozen')
    composition=data['alpha_composition'];require(composition['passed'] is True and composition['no_double_composition'] is True
            and composition['partial_alpha']==32768 and composition['tolerance_U8']==3 and integer(composition['max_U8_error']) and composition['max_U8_error']<=3
            and len(composition['expected'])==len(composition['observed'])==3
            and max(abs(a-b)for a,b in zip(composition['expected'],composition['observed']))==composition['max_U8_error'], 'actual half-alpha frame is composed exactly once')
    validate_window_evidence(data)
    export=data['export'];require(export['proof_enabled_during_export'] is True and export['RGBA16_exact'] is True and export['ICC_srgb'] is True and export['pixels']==307*211, 'proof-on full original export')
    return True


def validate_window_evidence(data):
    admission=data['window_capture_admission'];require(admission['accepted'] is True and admission['before_any_input'] is True and integer(admission['resize_count']) and admission['resize_count']<=1, 'full drawable admitted before input')
    dpr=admission['DPR'];require(finite(dpr)and dpr>0 and integer(admission['presented_frames'],3), 'admitted real DPR and presented frames')
    for dim in ('width','height'):
        require(integer(admission['logical_'+dim],1) and admission['physical_'+dim]==qt_round_positive(admission['logical_'+dim]*dpr), 'complete actual window dimensions')
    require([c['role']for c in data['window_captures']]==['relative','paper','gamut','off'], 'four complete stable proof/off windows')
    phases=data['phase_checkpoints'];require([p['phase']for p in phases]==list(range(15)) and all(integer(p['at_ms']) for p in phases)
            and all(a['at_ms']<=b['at_ms']for a,b in zip(phases,phases[1:])), 'ordered actual phase timing')
    source,revision=data['pointers'][0]['source_key'],data['pointers'][0]['render_revision']
    for c,phase in zip(data['window_captures'],(2,3,4,8)):
        require(c['phase']==phase and c['source_key']==source and c['render_revision']==revision and c['DPR']==dpr
                and all(c[k]==admission[k]for k in ('logical_width','logical_height','physical_width','physical_height'))
                and integer(c['presented_frames'],3) and phases[phase]['at_ms']<=c['at_ms']<=phases[phase+1]['at_ms'], 'current complete window with its stable source/revision/phase')
    return True


def validate_diagnostic(data, manifest, stages, performance, mode):
    require(manifest['adjustments']==data['initial_whole_Develop'], 'frozen original Develop')
    proof=performance['values']['soft_proof'];recorded=data['diagnostic']['proof']
    require(proof==recorded and proof['enabled'] is True and proof['available'] is True and proof['presentation_only'] is True
            and proof['export_or_scopes_modified'] is False and proof['profile_key']=='soft-proof-icc-v1:'+data['profile_sha256'], 'frozen presentation facts')
    require(str(stages['parameter_revision'])==data['render_revision'] and stages['gpu_context']['source_cache_key']==data['source_key'], 'frozen original source and revision')
    gpu=stages['gpu_working_output'];require(gpu['available'] is (mode=='gpu'), 'actual original-image GPU capture scope')
    if mode=='gpu':require(gpu['producer']=='gpu-compute' and gpu['numeric_source_policy']=='none' and gpu['monitor_icc'] is False
            and gpu['pixels']==307*211 and gpu['bytes']==307*211*16 and gpu['nonfinite_rgba']==[0,0,0,0], 'original GPU working frame remains independent from proof')
    return True


def validate_soft_proof(binary, environment, home, report, mode, *, expected_commit=None):
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
    argv=[str(binary),'--soft-proof-smoke-report',str(native),'--soft-proof-language',language]
    if qpa=='xcb' and not env.get('DISPLAY'):
        xvfb=shutil.which('xvfb-run',path=env.get('PATH'));require(xvfb is not None,'Xvfb required for real xcb App')
        argv=[xvfb,'-a','-s','-screen 0 1600x1100x24',*argv]
    pref_paths=[Path.home()/'Library/Preferences'/name for name in ('com.jixellight.JixelLight.plist','JixelLight.JixelLight.plist','com.JixelLight.JixelLight.plist')]if sys.platform=='darwin'else[]
    preference_states=lambda:{str(p):{'exists':p.is_file(),'sha256':file_sha(p)if p.is_file()else None}for p in pref_paths}
    before=preference_states();before_path=report.with_name(report.name+'.preferences-before.json')
    before_path.write_text(json.dumps({'scope':'inherited Python user home before any App','states':before},indent=2)+'\n')
    binary_sha=file_sha(binary);report.with_name(report.name+'.invocation.json').write_text(json.dumps({'argv':argv,'binary_sha256':binary_sha,'qpa':qpa,'language':language,'force_CPU':mode=='cpu','require_GPU':mode=='gpu','preferences_before_sha256':file_sha(before_path)},indent=2)+'\n')
    result={'mode':'soft-proof-'+mode,'passed':False,'native_path':str(native),'physical_device_verified':False,'real_printer_profile_verified':False,'native_float_RAW_accepted':False,'normal_Root_CI_accepted':False}
    stdout=report.with_name(report.name+'.stdout');stderr=report.with_name(report.name+'.stderr');start=time.monotonic()
    try:
        with stdout.open('xb')as out,stderr.open('xb')as err:
            try:code=subprocess.run(argv,cwd=home,env=env,stdout=out,stderr=err,timeout=75).returncode
            except subprocess.TimeoutExpired:code=124
        result['returncode']=code;data=json.loads(native.read_text());result['report']=data
        require(code==0,'App process exit');validate_report(data,mode,qpa,expected_commit)
        text=stderr.read_text(errors='replace')+stdout.read_text(errors='replace')
        errors=[x for x in text.splitlines()if any(k in x for k in ('TypeError:','ReferenceError:','Unable to assign [undefined]'))]
        result['qml_runtime_errors']=errors;require(not errors,'QML runtime error')
        base=str(native);source=Path(base+'.source.png')
        require(same_recorded_source(data['source_path'],str(source),qpa)and not source.is_symlink(),'owned original source')
        profile=Path(base+'.output.icc');require(same_recorded_source(data['profile_path'],str(profile),qpa)
                and not profile.is_symlink()and 0<profile.stat().st_size<=4*1024*1024 and file_sha(profile)==data['profile_sha256'],'original immutable ICC file')
        images={};pixels={}
        paths=[('source',source),('reference',Path(base+'.reference.png')),('wide',Path(base+'.wide.png')),('export',Path(base+'.export.png'))]
        for p in data['proofs']:require(same_recorded_source(p['path'],base+'.'+p['role']+'.proof.png',qpa),'original proof evidence path')
        paths.extend((p['role']+'-proof',Path(base+'.'+p['role']+'.proof.png'))for p in data['proofs'])
        for p in data['window_captures']:require(same_recorded_source(p['path'],base+'.'+p['role']+'.window.png',qpa),'original complete window path')
        paths.extend((p['role']+'-window',Path(base+'.'+p['role']+'.window.png'))for p in data['window_captures'])
        for role,path in paths:
            require(not path.is_symlink(),'owned PNG');row,decoded=png(path);images[role]=row;pixels[role]=decoded
            if role.endswith('-window'):
                admission=data['window_capture_admission']
                require((row['width'],row['height'],row['depth'])==(admission['physical_width'],admission['physical_height'],8) and row['color']in(2,6),'complete actual window PNG')
            else:
                require((row['width'],row['height'],row['depth'],row['color'])==(307,211,16,6),'every bounded original/proof RGBA16 pixel')
                if role in ('source','reference','wide','export'):require(row['ICC_bytes']>0,'original image ICC')
                if role=='source':require(row['file_sha256']==data['source_sha256'],'unchanged source SHA')
                else:require(decoded[6::8]==pixels['source'][6::8]and decoded[7::8]==pixels['source'][7::8],'every original alpha sample')
                if role.endswith('-proof'):
                    proof=next(p for p in data['proofs']if p['role']+'-proof'==role)
                    little=bytearray(decoded);little[0::2]=decoded[1::2];little[1::2]=decoded[0::2]
                    require(hashlib.sha256(little).hexdigest()==proof['expected_pixels_sha256'],'independently decoded complete proof pixels')
            with Path(base+'.'+role+'-byte-proof.json').open('x')as f:json.dump(row,f,indent=2);f.write('\n')
        require(pixels['export']==pixels['reference']and images['export']['ICC_sha256']==images['reference']['ICC_sha256']
                and images['export']['file_sha256']==data['export']['sha256'],'every proof-on export RGBA16 and ICC byte')
        histograms=[[0]*1024 for _ in range(4)]
        for r,g,b,a in struct.iter_unpack('>4H',pixels['reference']):
            for counts,v in zip(histograms,(r,g,b,(2126*r+7152*g+722*b+5000)//10000)):counts[v>>6]+=1
        require(all(data['scope'][key]==values for key,values in zip(('red','green','blue','luma'),histograms)),'independent full1024-bin original scopes')
        diagnostic=Path(base+'.diagnostic.zip');require(same_recorded_source(data['diagnostic']['path'],str(diagnostic),qpa)
                and not diagnostic.is_symlink()and file_sha(diagnostic)==data['diagnostic']['sha256'],'original frozen ZIP')
        with zipfile.ZipFile(diagnostic)as archive:
            validate_diagnostic(data,json.loads(archive.read('manifest.json')),json.loads(archive.read('stage_outputs.json')),json.loads(archive.read('performance.json')),mode)
            require(archive.getinfo('current_preview.png').file_size<32*1024*1024,'bounded original preview')
            payload=archive.read('current_preview.png')
        original=Path(base+'.original-diagnostic-preview.png')
        with original.open('xb')as f:f.write(payload)
        row,decoded=png(original);require(decoded==pixels['reference']and row['ICC_sha256']==images['reference']['ICC_sha256'],'frozen diagnostic retains original edit result')
        images['original-diagnostic-preview']=row
        require(before==preference_states()and file_sha(binary)==binary_sha and file_sha(source)==data['source_sha256'],'preferences/binary/source unchanged')
        result.update(passed=True,source_commit=data['source_commit'],backend=data['actual_backend'],language=language,first_PNG_byte_proofs=images,
            first_full_unfiltered_rows=sum(r['full_unfiltered_rows']for r in images.values()),preference_states_unchanged=True,
            native_report_sha256=file_sha(native),all_output_files={p.name:file_sha(p)for p in report.parent.glob(native.name+'*')if p.is_file()})
    except (OSError,ValueError,TypeError,KeyError,IndexError,AssertionError,struct.error,zipfile.BadZipFile)as error:
        result['error']=str(error)
    result.update(elapsed_ms=round((time.monotonic()-start)*1000),stdout_sha256=file_sha(stdout)if stdout.is_file()else None,stderr_sha256=file_sha(stderr)if stderr.is_file()else None)
    with report.open('x')as f:json.dump(result,f,indent=2);f.write('\n')
    return result


def main():
    parser=argparse.ArgumentParser();parser.add_argument('executable',type=Path);parser.add_argument('report',type=Path);parser.add_argument('mode',choices=('cpu','gpu'));parser.add_argument('--expected-commit');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='jixellight-soft-proof-')as home:
        result=validate_soft_proof(args.executable,os.environ.copy(),Path(home),args.report,args.mode,expected_commit=args.expected_commit)
    print(json.dumps({'mode':result['mode'],'passed':result['passed'],'source_commit':result.get('source_commit'),'error':result.get('error')}),flush=True)
    return 0 if result['passed']else 1

if __name__=='__main__':raise SystemExit(main())
