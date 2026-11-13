"""Independent byte, catalog and actual-system-trash evidence for owned move import."""
from __future__ import annotations
import argparse
from contextlib import closing
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath, PurePosixPath
import re
import shutil
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time
import zipfile
from ValidateRawPressureQpa import png, file_sha, require
from ValidateColorMask import integer, finite, positive_id, same_recorded_source, qt_round_positive, DEVELOP_FIELDS


def validate_operation_identities(data):
    ids = data['transfer_operation_ids']
    require(type(ids) is list and len(ids) == 2 and all(type(value) is str and re.fullmatch(r'[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}', value) is not None
            and value != '00000000-0000-0000-0000-000000000000' for value in ids) and len(set(ids)) == 2, 'distinct actual move/recovery operation identities')
    require(data['stale_transfer_cancel_refused'] is True and data['idle_transfer_operation_id'] == '', 'stale cancellation refused and completed operation idle')
    return True


def validate_report(data,mode,qpa,expected_commit=None):
    validate_operation_identities(data)
    require(mode in ('cpu','gpu')and qpa in ('cocoa','windows','xcb'),'actual mode/QPA')
    require(data['passed']is True and data['error']=='' and type(data['phase'])is int and data['phase']==12,'complete actual move/recovery workflow')
    require(data['language']==('en_US'if mode=='cpu'else'zh_CN')and data['actual_qpa']==qpa and data['GPU_active']is(mode=='gpu'),'language/QPA/processor')
    require(({'cocoa':'Metal','windows':'Direct3D 11','xcb':'OpenGL'}[qpa]if mode=='gpu'else'CPU').lower()in data['actual_backend'].lower(),'actual processing API')
    require(integer(data['elapsed_ms'])and data['elapsed_ms']<60000 and type(data['deadline_ms'])is int and data['deadline_ms']==60000,'unchanged native deadline')
    require(re.fullmatch(r'[0-9a-f]{7,40}',data['source_commit'])is not None,'compile stamp')
    if expected_commit:require(re.fullmatch(r'[0-9a-f]{7,40}',expected_commit)is not None and expected_commit.startswith(data['source_commit']),'exact expected build')
    for key in ('actual_Main_App_PhotoCanvas','controller_on_actual_GUI_thread','settings_isolated','actual_system_trash','owned_fixture_file_picker_setup','unselected_original_unchanged','durable_identities_verified','project_reopened','owned_interrupted_copy_fixture'):
        require(data[key]is True,'actual App/owned fixture evidence: '+key)
    for key in ('controlled_trash_service','native_file_picker_verified','forced_process_crash_verified','physical_device_verified','native_float_RAW_decoder','product_development_complete'):
        require(data[key]is False,'unsupported acceptance claim: '+key)
    require(type(data['initial_whole_Develop'])is dict and set(data['initial_whole_Develop'])==DEVELOP_FIELDS and data['initial_whole_Develop']==data['final_whole_Develop']
            and data['initial_whole_Develop']['exposure']==.2,'complete unchanged unselected Develop')
    require(type(data['initial_history'])is list and len(data['initial_history'])==2 and data['initial_history']==data['final_history'],'whole original durable history')
    require(type(data['catalog_count'])is int and data['catalog_count']==4 and positive_id(data['initial_source_key'])and positive_id(data['initial_render_revision'])and positive_id(data['final_source_key'])
            and positive_id(data['final_render_revision'])and int(data['final_render_revision'])>=int(data['initial_render_revision']),'original/catalog owners across reopen')
    require(data['reopened_source_key']==data['final_source_key']and data['reopened_render_revision']==data['final_render_revision']
            and data['reopened_owner_current']is True and integer(data['reopen_settled_ms'],200),'frozen final reopened owner')
    records=data['records'];require(all(type(r['index'])is int for r in records)and [r['index']for r in records]==[0,1,2],'two original moves and one reviewed recovery')
    source_paths=[];targets=[]
    for r in records:
        source=(PureWindowsPath if qpa=='windows'else PurePosixPath)(r['source'])
        require(source.is_absolute()and source.parent.name.startswith('JixelLight-owned-move-')and source.name.startswith('JixelLight-owned-')and source.suffix=='.png','only generated owned source files')
        require(r['state']=='trashed'and r['source_absent_after_system_trash']is True and r['durable_identity']is True and type(r['trash_path_reported'])is bool
                and integer(r['bytes'],1)and re.fullmatch(r'[0-9a-f]{64}',r['sha256'])is not None and re.fullmatch(r'[0-9a-f]{64}',r['journal_sha256'])is not None,'complete recoverable retirement after durable identity')
        source_paths.append(r['source']);targets.append(r['destination'])
    require(len(set(source_paths))==len(set(targets))==3 and records[0]['journal']==records[1]['journal']!=records[2]['journal'],'distinct selected sources/targets and recovery transaction')
    review=data['review'];require(review['read_only_before_click']is True and type(review['visible_rows'])is int and review['visible_rows']==1 and review['source_still_present']is True and review['catalog_count_before']==3
            and re.fullmatch(r'[0-9a-f]{64}',review['record_digest'])is not None,'concrete read-only recovery review before retirement')
    pointers=data['pointers'];require([p['target']for p in pointers]==['copyImportApply','moveImportResume'],'two actual QPA confirmation buttons')
    require([p['text']for p in pointers]==(['Start move','Verify and resume move']if mode=='cpu'else['开始移动','核对并继续移动']),'actual translated explicit move/recovery controls')
    for p in pointers:
        require(p['QPA_mouse_route']is True and p['source_key']==p['post_event_source_key']==data['initial_source_key']
                and p['render_revision']==p['post_event_render_revision']==data['initial_render_revision']and finite(p['x'])and finite(p['y'])and integer(p['at_ms']),'original frame unchanged by real pointer actions')
    require(pointers[0]['at_ms']<=pointers[1]['at_ms']<=data['elapsed_ms'],'ordered explicit input')
    scope=data['scope'];require(scope['pixels']==307*211 and scope['bins']==1024 and scope['four_histograms_exact']is True,'complete original statistics')
    require(scope['status']==('全分辨率统计'if data['language']=='zh_CN'else'Full-resolution statistics'),'current completed original full statistics')
    for key in ('red','green','blue','luma'):require(len(scope[key])==1024 and all(integer(v)for v in scope[key])and sum(scope[key])==scope['pixels'],'all original histogram bins')
    for key in ('shadow_clip_percent','highlight_clip_percent'):require(finite(scope[key])and 0<=scope[key]<=100,'original clipping facts')
    move=data['move'];require(move['phase']=='done'and move['retirement_backend']=='system-trash'and move['busy']is False and all(type(move[k])is int for k in ('requested','copied','retired'))and move['requested']==move['copied']==move['retired']==1
            and move['cancelled']is False and move['error']=='','actual final system-trash service')
    require(data['export']['RGBA16_exact']is True and data['export']['ICC_srgb']is True,'unselected original exact export')
    validate_windows(data);return True


def validate_windows(data):
    a=data['window_capture_admission'];require(a['accepted']is True and a['before_any_input']is True and integer(a['resize_count'])and a['resize_count']<=1,'complete actual drawable admitted before input')
    dpr=a['DPR'];require(finite(dpr)and dpr>0 and integer(a['presented_frames'],3),'actual DPR/presented frames')
    for dim in ('width','height'):require(integer(a['logical_'+dim],1)and a['physical_'+dim]==qt_round_positive(a['logical_'+dim]*dpr),'complete physical window dimensions')
    phases=data['phase_checkpoints'];require(all(type(p['phase'])is int for p in phases)and [p['phase']for p in phases]==list(range(12))and all(integer(p['at_ms'])for p in phases)
            and all(x['at_ms']<=y['at_ms']for x,y in zip(phases,phases[1:])),'ordered workflow phase evidence')
    captures=data['window_captures'];require([p['role']for p in captures]==['moved','recovery','recovered','reopened'],'four complete actual workflow windows')
    for capture,phase in zip(captures,(3,5,6,7)):
        require(capture['phase']==phase and capture['DPR']==dpr and integer(capture['presented_frames'],3)
                and all(capture[k]==a[k]for k in ('logical_width','logical_height','physical_width','physical_height'))
                and phases[phase]['at_ms']<=capture['at_ms']<=phases[phase+1]['at_ms'],'current complete capture with actual phase/DPR')
        key='final'if phase==7 else'initial'
        require(capture['source_key']==data[key+'_source_key']and capture['render_revision']==data[key+'_render_revision'],'current original-buffer owner for window')
        require(capture['preview_ready']is True and capture['source_available']is True and capture['loading']is False and capture['rendering']is False,'ready current frame at each capture')
    return True


def validate_diagnostic(data,manifest,stages,performance,mode):
    require(manifest['adjustments']==data['initial_whole_Develop']and performance['values']['move_import']==data['move'],'frozen original state and final transfer facts')
    require(str(stages['parameter_revision'])==data['final_render_revision']and stages['gpu_context']['source_cache_key']==data['final_source_key'],'frozen current original owner')
    gpu=stages['gpu_working_output'];require(gpu['available']is(mode=='gpu'),'actual GPU evidence scope')
    if mode=='gpu':require(gpu['producer']=='gpu-compute'and gpu['numeric_source_policy']=='none'and gpu['monitor_icc']is False and gpu['pixels']==307*211
            and gpu['bytes']==307*211*16 and gpu['nonfinite_rgba']==[0,0,0,0],'unchanged complete original GPU working output')
    return True


def validate_catalog(data):
    project=Path(data['project_path']).resolve();db=project/'Project.db';require(project.suffix.lower()=='.jlp'and db.is_file()and not db.is_symlink(),'actual saved catalog')
    before=file_sha(db)
    with closing(sqlite3.connect(db.as_uri()+'?mode=ro&immutable=1',uri=True))as catalog:
        photos=catalog.execute('SELECT path,adjustment_json FROM photos').fetchall();identities=catalog.execute('SELECT source_key,schema,size_bytes,sha256 FROM original_identity').fetchall()
    def path(key):
        p=Path(key);return(p if p.is_absolute()else project/p).resolve()
    expected={Path(data['baseline_path']).resolve(),*(Path(row['destination']).resolve()for row in data['records'])}
    require(len(photos)==4 and {path(p)for p,j in photos}==expected,'independent complete catalog source set')
    baseline=next(json.loads(j)for p,j in photos if path(p)==Path(data['baseline_path']).resolve())
    require({k:v for k,v in baseline.items()if not k.startswith('_')}==data['initial_whole_Develop']and type(baseline['_history'])is dict,'saved original Develop/history')
    mapped={path(key):(schema,size,sha)for key,schema,size,sha in identities}
    for row in data['records']:require(mapped.get(Path(row['destination']).resolve())==(1,row['bytes'],row['sha256']),'independent saved destination SHA/size identity')
    require(file_sha(db)==before,'read-only catalog inspection');return before


def validate_move_import(binary, environment, home, report, mode, *, expected_commit=None):
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
    argv=[str(binary),'--move-import-smoke-report',str(native),'--move-import-language',language]
    if qpa=='xcb' and not env.get('DISPLAY'):
        xvfb=shutil.which('xvfb-run',path=env.get('PATH'));require(xvfb is not None,'Xvfb required for real xcb App')
        argv=[xvfb,'-a','-s','-screen 0 1600x1100x24',*argv]
    pref_paths=[Path.home()/'Library/Preferences'/name for name in ('com.jixellight.JixelLight.plist','JixelLight.JixelLight.plist','com.JixelLight.JixelLight.plist')]if sys.platform=='darwin'else[]
    preference_states=lambda:{str(p):{'exists':p.is_file(),'sha256':file_sha(p)if p.is_file()else None}for p in pref_paths}
    before=preference_states();before_path=report.with_name(report.name+'.preferences-before.json')
    before_path.write_text(json.dumps({'scope':'inherited Python user home before any App','states':before},indent=2)+'\n')
    binary_sha=file_sha(binary);report.with_name(report.name+'.invocation.json').write_text(json.dumps({'argv':argv,'binary_sha256':binary_sha,'qpa':qpa,'language':language,'force_CPU':mode=='cpu','require_GPU':mode=='gpu','preferences_before_sha256':file_sha(before_path)},indent=2)+'\n')
    result={'mode':'move-import-'+mode,'passed':False,'native_path':str(native),'actual_system_trash':True,'physical_device_verified':False,'normal_Root_CI_accepted':False}
    stdout=report.with_name(report.name+'.stdout');stderr=report.with_name(report.name+'.stderr');start=time.monotonic()
    try:
        with stdout.open('xb')as out,stderr.open('xb')as err:
            try:code=subprocess.run(argv,cwd=home,env=env,stdout=out,stderr=err,timeout=75).returncode
            except subprocess.TimeoutExpired:code=124
        result['returncode']=code;data=json.loads(native.read_text());result['report']=data
        require(code==0,'App process exit');validate_report(data,mode,qpa,expected_commit)
        text=stderr.read_text(errors='replace')+stdout.read_text(errors='replace')
        errors=[line for line in text.splitlines()if any(t in line for t in ('TypeError:','ReferenceError:','Unable to assign [undefined]'))]
        result['qml_runtime_errors']=errors;require(not errors,'QML runtime error')
        base=str(native);baseline=Path(base+'.baseline-source.png')
        require(same_recorded_source(data['baseline_path'],str(baseline),qpa)and not baseline.is_symlink()and file_sha(baseline)==data['baseline_sha256'],'owned unselected baseline')
        images={};decoded={};paths=[('baseline',baseline),('reference',Path(base+'.reference.png')),('export',Path(base+'.export.png'))]
        directory=Path(base+'.destination').resolve()
        for row in data['records']:
            backup=Path(base+'.source-'+str(row['index'])+'-before.png');target=Path(row['destination']).resolve()
            require(same_recorded_source(row['before'],str(backup),qpa)and target.parent==directory and not target.is_symlink()
                    and target.stat().st_size==row['bytes']and file_sha(target)==file_sha(backup)==row['sha256'],'every full original copy byte before retirement')
            paths.extend([('before-'+str(row['index']),backup),('copy-'+str(row['index']),target)])
            journal=Path(row['journal']);require(journal.is_file()and not journal.is_symlink()and journal.stat().st_size<=2*1024*1024
                    and journal.parent.parent.resolve()==directory and file_sha(journal)==row['journal_sha256'],'bounded original recovery journal')
            record=json.loads(journal.read_text());require(record['schema']==1 and record['phase']=='done'and record['error']==''and record['cancelled']is False,'actual completed recovery record')
            entry=next(e for e in record['files']if same_recorded_source(e['source'],row['source'],qpa))
            require(same_recorded_source(entry['destination'],row['destination'],qpa)and entry['sha256']==row['sha256']and entry['bytes']==row['bytes']and entry['state']=='trashed','exact original journal file identity')
        reviewed=Path(base+'.reviewed-move.json');require(not reviewed.is_symlink()and file_sha(reviewed)==data['review']['record_digest'],'exact immutable visible review bytes')
        frozen=json.loads(reviewed.read_text());last=data['records'][2]
        require(frozen['phase']=='copied'and len(frozen['files'])==1 and frozen['files'][0]['state']=='copied'
                and same_recorded_source(frozen['files'][0]['source'],last['source'],qpa)and frozen['files'][0]['sha256']==last['sha256'],'review precedes destination catalog handoff and retirement')
        for capture in data['window_captures']:
            path=Path(base+'.'+capture['role']+'.window.png');require(same_recorded_source(capture['path'],str(path),qpa),'original full-window evidence path');paths.append((capture['role']+'-window',path))
        for role,path in paths:
            require(not path.is_symlink(),'owned original PNG');row,pixels=png(path);images[role]=row;decoded[role]=pixels
            if role.endswith('-window'):
                admission=data['window_capture_admission'];require((row['width'],row['height'],row['depth'])==(admission['physical_width'],admission['physical_height'],8)and row['color']in(2,6),'complete actual physical window')
            else:require((row['width'],row['height'],row['depth'],row['color'])==(307,211,16,6)and row['ICC_bytes']>0,'complete own RGBA16 and ICC')
            with Path(base+'.'+role+'-byte-proof.json').open('x')as f:json.dump(row,f,indent=2);f.write('\n')
        for index in range(3):require(decoded['before-'+str(index)]==decoded['copy-'+str(index)]and images['before-'+str(index)]['ICC_sha256']==images['copy-'+str(index)]['ICC_sha256'],'every copied RGBA16/ICC sample')
        require(decoded['reference']==decoded['export']and images['reference']['ICC_sha256']==images['export']['ICC_sha256']
                and images['export']['file_sha256']==data['export']['sha256'],'exact unchanged unselected export/ICC')
        for role in ('reference','export'):require(decoded[role][6::8]==decoded['baseline'][6::8]and decoded[role][7::8]==decoded['baseline'][7::8],'every original alpha channel')
        histograms=[[0]*1024 for _ in range(4)];shadows=highlights=0
        for r,g,b,a in struct.iter_unpack('>4H',decoded['reference']):
            for counts,v in zip(histograms,(r,g,b,(2126*r+7152*g+722*b+5000)//10000)):counts[v>>6]+=1
            shadows+=r<=257 and g<=257 and b<=257;highlights+=r>=65278 or g>=65278 or b>=65278
        require(all(data['scope'][key]==values for key,values in zip(('red','green','blue','luma'),histograms)),'independent original full1024-bin statistics')
        require(abs(data['scope']['shadow_clip_percent']-100*shadows/(307*211))<1.e-9 and abs(data['scope']['highlight_clip_percent']-100*highlights/(307*211))<1.e-9,'independent unchanged clipping')
        catalog_sha=validate_catalog(data)
        diagnostic=Path(base+'.diagnostic.zip');require(same_recorded_source(data['diagnostic']['path'],str(diagnostic),qpa)and file_sha(diagnostic)==data['diagnostic']['sha256'],'original frozen diagnostic ZIP')
        with zipfile.ZipFile(diagnostic)as archive:
            validate_diagnostic(data,json.loads(archive.read('manifest.json')),json.loads(archive.read('stage_outputs.json')),json.loads(archive.read('performance.json')),mode)
            require(archive.getinfo('current_preview.png').file_size<32*1024*1024,'bounded frozen preview');payload=archive.read('current_preview.png')
        original=Path(base+'.original-diagnostic-preview.png')
        with original.open('xb')as f:f.write(payload)
        row,pixels=png(original);require(pixels==decoded['reference']and row['ICC_sha256']==images['reference']['ICC_sha256'],'frozen original image pixels and ICC');images['original-diagnostic-preview']=row
        require(before==preference_states()and file_sha(binary)==binary_sha and file_sha(baseline)==data['baseline_sha256'],'App preferences/binary/unselected source unchanged')
        outputs=[]
        for path in report.parent.glob(native.name+'*'):
            candidates=path.rglob('*')if path.is_dir()else[path]
            for file in candidates:
                require(not file.is_symlink(),'owned evidence without symlinks')
                if file.is_file():outputs.append(file)
        result.update(passed=True,source_commit=data['source_commit'],backend=data['actual_backend'],language=language,first_PNG_byte_proofs=images,
            first_full_unfiltered_rows=sum(r['full_unfiltered_rows']for r in images.values()),catalog_sha256=catalog_sha,preference_states_unchanged=True,
            native_report_sha256=file_sha(native),all_output_files={str(p.relative_to(report.parent)):file_sha(p)for p in outputs})
    except(OSError,ValueError,TypeError,KeyError,IndexError,StopIteration,AssertionError,struct.error,sqlite3.Error,zipfile.BadZipFile)as error:result['error']=str(error)
    result.update(elapsed_ms=round((time.monotonic()-start)*1000),stdout_sha256=file_sha(stdout)if stdout.is_file()else None,stderr_sha256=file_sha(stderr)if stderr.is_file()else None)
    with report.open('x')as f:json.dump(result,f,indent=2);f.write('\n')
    return result


def main():
    parser=argparse.ArgumentParser();parser.add_argument('executable',type=Path);parser.add_argument('report',type=Path);parser.add_argument('mode',choices=('cpu','gpu'));parser.add_argument('--expected-commit');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='jixellight-move-import-')as home:
        result=validate_move_import(args.executable,os.environ.copy(),Path(home),args.report,args.mode,expected_commit=args.expected_commit)
    print(json.dumps({'mode':result['mode'],'passed':result['passed'],'source_commit':result.get('source_commit'),'error':result.get('error')}),flush=True)
    return 0 if result['passed']else 1

if __name__=='__main__':raise SystemExit(main())
