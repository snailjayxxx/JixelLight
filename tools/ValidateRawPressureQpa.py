"""Validate finite unique pressure packets through the normal photo-canvas App."""
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
import zlib

FALSE_SCOPE = ('physical_tablet_verified', 'physical_OS_activation_verified',
               'arbitrary_or_unbounded_raw_jitter_accepted', 'normal_Root_or_CI_accepted',
               'native_float_RAW_decoder', 'product_development_complete')

def require(value, message):
    if not value:
        raise ValueError(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def file_sha(path):
    with Path(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def validate_report(data, mode, qpa, expected_commit=None):
    require(mode in ('cpu','gpu') and data['passed'] is True and not data['error'], 'App did not pass')
    require(data['language']==('en_US' if mode=='cpu' else 'zh_CN') and data['actual_qpa']==qpa, 'language/QPA')
    require(data['GPU_active'] is (mode=='gpu') and data['settings_isolated'] is True, 'backend/settings isolation')
    require(all(data[k] is True for k in ('actual_photo_app_UI','controlled_qpa_tablet','source_unchanged')), 'actual owned App')
    require(all(data[k] is False for k in FALSE_SCOPE), 'unsupported scope claim')
    require(type(data['elapsed_ms']) is int and 0<=data['elapsed_ms']<60000 and data['deadline_ms']==60000, 'deadline')
    require(type(data['GUI_ticks']) is int and data['GUI_ticks']>0
            and type(data['maximum_send_ms']) in (int,float) and math.isfinite(data['maximum_send_ms'])
            and data['maximum_send_ms']>=0, 'GUI evidence')
    require(type(data['dpr']) in (int,float) and math.isfinite(data['dpr']) and data['dpr']>0, 'DPR')
    require(re.fullmatch(r'(?:[0-9a-f]{7,40}|unknown)',data['source_commit']) is not None, 'compile stamp')
    if expected_commit:
        require(re.fullmatch(r'[0-9a-f]{7,40}',expected_commit) is not None
                and data['source_commit'].startswith(expected_commit), 'expected commit')
    backend=data['actual_backend']
    require(isinstance(backend,str) and bool(backend), 'actual backend')
    if mode=='gpu':
        api={'cocoa':'Metal','windows':'Direct3D 11','xcb':'OpenGL'}[qpa]
        require(api.lower() in backend.lower(), 'actual GPU API')
    else:
        require('CPU' in backend, 'actual CPU backend')
    rounds=data['rounds']
    require(len(rounds)==2 and [r['round']for r in rounds]==[0,1]
            and [r['mode']for r in rounds]==['paint','erase'], 'complete ordered rounds')
    for row in rounds:
        require(row['passed'] is True and type(row['held_packets']) is int and row['held_packets']==7003
                and type(row['sampler_dabs']) is int and 2000<=row['sampler_dabs']<=4096, 'complete sampler')
        require(row['complete_RGBA64_pixels']==64777 and row['pixel_sha256']==row['original_pixel_sha256'], 'complete pixel oracle')
        require(row['Undo_Redo_and_persistence_bit_exact'] is True and row['source_unchanged'] is True
                and row['physical_or_arbitrary_jitter_claim'] is False, 'history/source scope')
    return True

def validate_trace(row, round_index, source_sha):
    require(row['passed'] is True and row['round']==round_index and row['source_file_sha256']==source_sha, 'trace scope')
    require(type(row['held_packets']) is int and type(row['unique_delivered_positions']) is int
            and row['held_packets']==row['unique_delivered_positions']==7003
            and type(row['original_sampler_dabs']) is int and 2000<=row['original_sampler_dabs']<=4096, 'trace counts')
    require(row['release_pressure']==0 and row['default_stabilizer_off'] is True and row['single_history_commit'] is True, 'raw rule/history')
    require(row['first_published_sample']=='1' and row['final_published_sample']=='7003'
            and row['sample1_published_before_final'] is True and type(row['held_GUI_ticks']) is int
            and row['held_GUI_ticks']>0, 'live publication')
    require(row['physical_or_arbitrary_jitter_claim'] is False
            and row['reference_uses_public_delivered_QML_source_mapping'] is True, 'trace scope claims')
    require(all(isinstance(row[k],str) and row[k].isdigit() and int(row[k])>0 for k in ('gesture','epoch','source_key')), 'trace ownership')
    require(isinstance(row['catalog'],str) and row['catalog'].isdigit() and bool(row['photo_key']), 'trace target')
    require(len(row['trace'])==7003, 'full stored trace')
    positions=set();fingerprint=hashlib.sha256();pressures=(.25,.875,.375,1.,.125,.75,.5,0.)
    for i,entry in enumerate(row['trace']):
        require(len(entry)==5 and type(entry[0]) is int and entry[0]==i, 'ordered raw packet')
        _,x,y,p,t=entry
        require(all(type(v) in (int,float) and math.isfinite(v) for v in (x,y,p)) and 0<=x<=1 and 0<=y<=1, 'finite source mapping')
        require(p==(.125 if i==7002 else pressures[i%8]) and t==str(2002+round_index*14008+i*2), 'raw pressure/time')
        positions.add((x,y));fingerprint.update(struct.pack('>I3dQ',i,x,y,p,int(t)))
    require(len(positions)==7003, 'unique delivered positions')
    return fingerprint.hexdigest()

def empty_brush_document(value):
    if type(value) is not dict:
        return False
    if not value:
        return True
    return (set(value)=={'schema','layers'} and type(value['schema']) is int
            and value['schema'] in (1,2) and type(value['layers']) is list and not value['layers'])

def inflate(data, maximum):
    decoder = zlib.decompressobj()
    result = decoder.decompress(data, maximum + 1)
    assert len(result) <= maximum and decoder.eof
    assert not decoder.unused_data and not decoder.unconsumed_tail
    return result

def png(path):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n' and len(data) < 32 * 1024 * 1024
    offset = 8
    chunks = []
    image = bytearray()
    icc = None
    header = None
    ended = False
    after_idat = False
    while offset < len(data):
        assert offset + 12 <= len(data)
        length, = struct.unpack_from('>I', data, offset)
        end = offset + 12 + length
        assert end <= len(data)
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:end - 4]
        assert zlib.crc32(kind + payload) & 0xffffffff == struct.unpack_from('>I', data, end - 4)[0]
        if not chunks:
            assert kind == b'IHDR'
        if kind == b'IHDR':
            assert header is None and length == 13
            header = struct.unpack('>2I5B', payload)
        elif kind == b'IDAT':
            assert not after_idat
            image.extend(payload)
        else:
            if image:
                after_idat = True
            if kind == b'iCCP':
                assert icc is None and not image
                name, compressed = payload.split(b'\0', 1)
                assert 1 <= len(name) <= 79 and compressed[0] == 0
                icc = inflate(compressed[1:], 2 * 1024 * 1024)
                assert len(icc) >= 128 and struct.unpack_from('>I', icc)[0] == len(icc) and icc[36:40] == b'acsp'
            if kind == b'IEND':
                assert not payload and end == len(data)
                ended = True
        chunks.append(kind.decode('ascii'))
        offset = end
    assert ended and header and image
    w, h, depth, color, compression, filtering, interlace = header
    assert 0 < w <= 8192 and 0 < h <= 8192
    assert depth in (8, 16) and color in (2, 6) and (compression, filtering, interlace) == (0, 0, 0)
    bpp = (3 if color == 2 else 4) * (depth // 8)
    stride = w * bpp
    expected = (stride + 1) * h
    assert expected <= 64 * 1024 * 1024
    encoded = inflate(image, expected)
    assert len(encoded) == expected
    previous = bytearray(stride)
    pixels = bytearray()
    filters = [0] * 5
    for y in range(h):
        kind = encoded[y * (stride + 1)]
        assert kind <= 4
        filters[kind] += 1
        row = bytearray(encoded[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = row[x - bpp] if x >= bpp else 0
            b = previous[x]
            c = previous[x - bpp] if x >= bpp else 0
            prediction = 0
            if kind == 1:
                prediction = a
            elif kind == 2:
                prediction = b
            elif kind == 3:
                prediction = (a + b) // 2
            elif kind == 4:
                estimate = a + b - c
                distances = (abs(estimate-a), abs(estimate-b), abs(estimate-c))
                prediction = (a, b, c)[distances.index(min(distances))]
            row[x] = (row[x] + prediction) & 255
        pixels.extend(row)
        previous = row
    native = bytearray(pixels)
    if depth == 16 and sys.byteorder == 'little':
        native[0::2], native[1::2] = pixels[1::2], pixels[0::2]
    result = {'path': str(path), 'file_sha256': sha(data), 'file_bytes': len(data),
              'width': w, 'height': h, 'depth': depth, 'color': color,
              'CRC_all_chunks': True, 'full_unfiltered_rows': h, 'filters': filters,
              'bounded_zlib_complete': True, 'no_trailing_bytes': True,
              'chunks': chunks, 'decoded_native_pixel_sha256': sha(native),
              'ICC_bytes': len(icc) if icc else 0, 'ICC_sha256': sha(icc) if icc else None}
    return result, bytes(pixels)

def validate_raw_qpa(binary, environment, home, report, mode, *, expected_commit=None):
    binary,home,report=Path(binary).resolve(),Path(home).resolve(),Path(report).resolve()
    require(mode in ('cpu','gpu') and binary.is_file(), 'binary/mode')
    require(not report.exists() and not report.is_symlink() and not list(report.parent.glob(report.stem+'.*')), 'fresh report namespace')
    report.parent.mkdir(parents=True,exist_ok=True);home.mkdir(parents=True,exist_ok=True)
    (home/'tmp').mkdir(exist_ok=True)
    env=environment.copy()
    for key in ('JIXELLIGHT_REQUIRE_GPU','JIXELLIGHT_FORCE_CPU','JIXELLIGHT_DISABLE_GPU',
                'JIXELLIGHT_QPA_ERASE_SEED','QSG_RHI_BACKEND','QSG_RHI_PREFER_SOFTWARE_RENDERER'):
        env.pop(key,None)
    for key in ('HOME','USERPROFILE','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','APPDATA','LOCALAPPDATA'):
        env[key]=str(home)
    env['TMPDIR']=str(home/'tmp');env['QML_DISABLE_DISK_CACHE']='1';env['JIXELLIGHT_CPU_THREADS']='2'
    qpa='windows' if sys.platform=='win32' else 'cocoa' if sys.platform=='darwin' else 'xcb'
    env['QT_QPA_PLATFORM']=qpa
    if mode=='cpu':
        env['JIXELLIGHT_FORCE_CPU']='1'
    else:
        env['JIXELLIGHT_REQUIRE_GPU']='1'
        env['QSG_RHI_BACKEND']='d3d11' if qpa=='windows' else 'metal' if qpa=='cocoa' else 'opengl'
        if qpa=='windows':env['QSG_RHI_PREFER_SOFTWARE_RENDERER']='1'
        if qpa=='xcb':env['LIBGL_ALWAYS_SOFTWARE']='1'
    native=report.with_name(report.stem+'.native.json')
    language='en_US' if mode=='cpu' else 'zh_CN'
    argv=[str(binary),'--brush-raw-qpa-smoke-report',str(native),'--brush-raw-qpa-language',language]
    if qpa=='xcb' and not env.get('DISPLAY'):
        xvfb=shutil.which('xvfb-run',path=env.get('PATH'))
        require(xvfb is not None, 'Xvfb required for real xcb App')
        argv=[xvfb,'-a','-s','-screen 0 1280x960x24',*argv]
    pref_paths=[Path.home()/'Library/Preferences'/name for name in
                ('com.jixellight.JixelLight.plist','JixelLight.JixelLight.plist','com.JixelLight.JixelLight.plist')] if sys.platform=='darwin' else []
    preference_states=lambda:{str(p):{'exists':p.is_file(),'sha256':file_sha(p) if p.is_file() else None}for p in pref_paths}
    before=preference_states()
    before_path=report.with_name(report.name+'.preferences-before.json')
    before_path.write_text(json.dumps({'scope':'inherited Python user home before child isolation','states':before},indent=2)+'\n')
    invocation=report.with_name(report.name+'.invocation.json')
    binary_sha=file_sha(binary)
    invocation.write_text(json.dumps({'argv':argv,'binary_sha256':binary_sha,'qpa':qpa,'language':language,
        'force_CPU':mode=='cpu','require_GPU':mode=='gpu','before_preferences_sha256':file_sha(before_path),
        'private_recovery_seed_overlay_used':False},indent=2)+'\n')
    result={'mode':'raw-qpa-'+mode,'passed':False,'physical_tablet_verified':False,'arbitrary_raw_jitter_accepted':False,
            'native_float_RAW_accepted':False,'normal_Root_CI_accepted':False,'native_path':str(native)}
    start=time.monotonic()
    stdout=report.with_name(report.name+'.stdout');stderr=report.with_name(report.name+'.stderr')
    try:
        with stdout.open('xb')as out,stderr.open('xb')as err:
            try:code=subprocess.run(argv,cwd=home,env=env,stdout=out,stderr=err,timeout=75).returncode
            except subprocess.TimeoutExpired:code=124
        result['returncode']=code
        data=json.loads(native.read_text());result['report']=data
        require(code==0,'App process exit')
        validate_report(data,mode,qpa,expected_commit)
        text=stderr.read_text(errors='replace')+stdout.read_text(errors='replace')
        errors=[line for line in text.splitlines() if any(k in line for k in ('TypeError:','ReferenceError:','Unable to assign [undefined]'))]
        result['qml_runtime_errors']=errors;require(not errors,'QML runtime errors')
        source=Path(str(native)+'.source.png');require(data['source_path']==str(source) and not source.is_symlink(),'owned source path')
        source_row,source_pixels=png(source)
        require(source_row['file_sha256']==data['source_file_sha256'] and source_row['depth']==16
                and source_row['color']==6 and (source_row['width'],source_row['height'])==(307,211),'owned source byte framing')
        with Path(str(native)+'.source-byte-proof.json').open('x') as proof:
            json.dump(source_row,proof,indent=2);proof.write('\n')
        images={'source':source_row};streams=[];completes=[];trace_digests=[]
        for index,completed in enumerate(data['rounds']):
            stem=str(native)+'.round'+str(index)
            stream=json.loads(Path(stem+'-stream.json').read_text());streams.append(stream)
            complete=json.loads(Path(stem+'-complete.json').read_text());completes.append(complete)
            require(complete==completed,'complete checkpoint binding')
            trace_digests.append(validate_trace(stream,index,data['source_file_sha256']))
            require(stream['original_sampler_dabs']==completed['sampler_dabs'],'sampler count binding')
            for role in ('pixels','window'):
                path=Path(stem+'-'+role+'.png');require(not path.is_symlink(),'owned PNG')
                row,pixels=png(path);images[str(index)+'-'+role]=row
                if role=='pixels':
                    require(completed['PNG16_path']==str(path) and row['file_sha256']==completed['PNG16_sha256'],'pixel file binding')
                    require((row['width'],row['height'],row['depth'],row['color'])==(307,211,16,6)
                            and row['ICC_bytes']>0 and row['decoded_native_pixel_sha256']==completed['pixel_sha256'],'exact PNG16/ICC')
                    require(all(pixels[i:i+2]==source_pixels[i:i+2]for i in range(6,len(pixels),8)),'all exact source alpha')
                else:
                    require(completed['window_path']==str(path) and row['file_sha256']==completed['window_sha256'],'window binding')
                    require(row['width']==round(1180*data['dpr']) and row['height']==round(720*data['dpr']),'complete window')
                with Path(stem+'-'+role+'-byte-proof.json').open('x') as proof:
                    json.dump(row,proof,indent=2);proof.write('\n')
        first,last=streams
        require(first['applied']==last['before'] and first['gesture']!=last['gesture'],'continuous ordered document/gesture')
        require(all(first[k]==last[k]for k in ('epoch','catalog','source_key','photo_key')),'stable target/source')
        require(empty_brush_document(first['before']) and len(first['applied']['layers'])==len(last['applied']['layers'])==1,'whole document')
        layer=last['applied']['layers'][0]
        require(len(layer['strokes'])==2 and layer['strokes'][1]['mode']=='erase' and last['actual_selected_layer']==layer['id'],'actual Erase selected layer')
        paint=layer['strokes'][0].copy();require(paint.pop('mode','paint')=='paint','ordered Paint mode')
        original=first['applied']['layers'][0]['strokes'][0].copy();original.pop('mode',None)
        require(paint==original and all(layer[k]==first['applied']['layers'][0][k]for k in ('id','enabled','exposure')),'retained exact Paint baseline')
        require(before==preference_states() and file_sha(binary)==binary_sha and file_sha(source)==data['source_file_sha256'],'preferences/binary/source unchanged')
        result.update({'passed':True,'source_commit':data['source_commit'],'backend':data['actual_backend'],
            'language':language,'complete_rounds':2,'held_packets_each':7003,'unique_positions_each':7003,
            'stored_trace_fingerprints':trace_digests,'first_PNG_byte_proofs':images,
            'first_full_unfiltered_rows':sum(r['full_unfiltered_rows']for r in images.values()),
            'preference_states_unchanged':True,'native_report_sha256':file_sha(native),
            'all_output_files':{p.name:file_sha(p)for p in report.parent.glob(native.name+'*')if p.is_file()}})
    except (OSError,ValueError,TypeError,KeyError,IndexError,AssertionError,struct.error)as error:
        result['error']=str(error)
    result.update({'elapsed_ms':round((time.monotonic()-start)*1000),
        'stdout_sha256':file_sha(stdout) if stdout.is_file() else None,
        'stderr_sha256':file_sha(stderr) if stderr.is_file() else None})
    with report.open('x')as f:json.dump(result,f,indent=2);f.write('\n')
    return result

def main():
    parser=argparse.ArgumentParser();parser.add_argument('executable',type=Path);parser.add_argument('report',type=Path)
    parser.add_argument('mode',choices=('cpu','gpu'));parser.add_argument('--expected-commit')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='jixellight-raw-qpa-')as home:
        result=validate_raw_qpa(args.executable,os.environ.copy(),Path(home),args.report,args.mode,expected_commit=args.expected_commit)
    print(json.dumps({'mode':result['mode'],'passed':result['passed'],'source_commit':result.get('source_commit'),'error':result.get('error')}),flush=True)
    return 0 if result['passed'] else 1

if __name__=='__main__':
    raise SystemExit(main())
