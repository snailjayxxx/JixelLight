"""Strict XMP normal-App schema and full PNG decoder; source/job acceptance stays independent."""
from pathlib import Path
from datetime import datetime
import hashlib,json,math,re,struct,zlib
BUDGET=3*1024**3
FALSE=('QML_menu_pointer_exercised','Root_or_CI_or_F2_completion','annotations_undoable','atomic_filesystem_snapshot_claim','new_native_brush_or_RAW_or_fullcolour_or_all_backend_acceptance','physical_deviceLoss_tablet_OOM_acceptance')
TRUE=('passed','Develop_undo_restored','UI_fields_match','annotations_preserved_after_Develop_undo','busy_observed','file_picker_accepted','full_stored_record_matches','normal_App_main_and_QML_window','normal_import_method_called','original_unchanged','review_widget_pointer_and_key_input','settings_isolated','sidecar_unchanged','stored_undo_and_redo_history')
TITLES={'en_US':'Review XMP import fields','zh_CN':'逐项检查 XMP 导入'}
NOTICES={'en_US':'Develop selects the complete snapshot (summary shown above). Develop undo remains available; catalog annotations are not undoable. The sidecar file is not rewritten.','zh_CN':'显影参数按整个快照选择（上方为摘要）。原有显影撤销保持可用；目录标注暂不可撤销。侧车文件不会被改写。'}
def need(v,m):
 if not v:raise ValueError(m)
def integer(v,lo,hi=None):need(type(v)is int and lo<=v<=(lo if hi is None else hi),'typed bounded integer')
def digest(v):need(type(v)is str and re.fullmatch('[0-9a-f]{64}',v)is not None,'SHA256')
def finite(d):
 if isinstance(d,dict):
  for x in d.values():finite(x)
 elif isinstance(d,list):
  for x in d:finite(x)
 elif type(d)in(int,float):need(math.isfinite(d),'finite JSON')
def exact(a,b):
 finite(a);finite(b);need(json.dumps(a,sort_keys=True,allow_nan=False,separators=(',',':'))==json.dumps(b,sort_keys=True,allow_nan=False,separators=(',',':')),'typed full JSON equality')
def stamp(s):need(type(s)is str and s.endswith('Z'),'UTC producer time');return datetime.fromisoformat(s.replace('Z','+00:00'))
def sequence(s):need(type(s)is str and re.fullmatch('[1-9][0-9]{0,18}',s)is not None,'actual decimal sequence/epoch');return int(s)
def fixture_develop(exposure):
 d={k:0 for k in ['blacks','contrast','highlightRecovery','highlights','hue','saturation','shadows','temperature','tint','vibrance','whites']};d['exposure']=exposure
 for k in ['blueCurve','greenCurve','masterCurve','redCurve']:d[k]=[0,.25,.5,.75,1]
 for k in ['hslHue','hslLuminance','hslSaturation']:d[k]=[0]*8
 d['look']={'code':'','mode':'off','parameters':{},'schema':1,'strength':1};d['geometry']={'flipHorizontal':False,'flipVertical':False,'height':1,'quarterTurns':0,'schema':1,'width':1,'x':0,'y':0};return d

def validate(d,language,*,expected_commit=None,private_example=False):
 need(language in TITLES and d['language']==language and d['mode']=='xmp-normal-App-merge' and d['error']=='','actual new mode/language')
 for k in TRUE:need(d[k]is True,k)
 for k in FALSE:need(d[k]is False,'scope overclaim '+k)
 for k,n in [('schema',1),('phase',7),('GUI_deadline_ms',60000),('conflict_choices',6),('budget_limit_bytes',BUDGET)]:integer(d[k],n)
 integer(d['elapsed_ms'],1,59999);integer(d['peak_operation_reserved_bytes'],0,BUDGET);integer(d['afterFrameEnd_count'],1,2**31)
 need(type(d['actual_DPR'])in(int,float)and math.isfinite(d['actual_DPR'])and .5<=d['actual_DPR']<=8,'actual finite DPR')
 need((d['actual_qpa'],d['actual_scene_backend'])in [('cocoa','Metal'),('windows','Direct3D 11'),('xcb','OpenGL')],'actual App QPA/backend pair; component offscreen not App')
 s=d['source_commit'];need(type(s)is str,'commit string')
 if not(private_example and expected_commit is None and s=='unknown'):
  need(re.fullmatch('[0-9a-f]{7,40}',s)is not None,'actual compiled commit; no private unknown promotion')
  if expected_commit is not None:need(re.fullmatch('[0-9a-f]{7,40}',expected_commit)is not None and expected_commit.startswith(s),'actual expected compiled commit')
 need(d['review_title']==TITLES[language] and d['review_notice']==NOTICES[language],'real bilingual labels and honest annotation undo policy')
 digest(d['original_source_SHA256']);digest(d['sidecar_SHA256'])
 exact(d['UI_after'],{'albums':['External'],'exposure':1.25,'flag':'pick','keywords':['incoming'],'label':'red','rating':4})
 expected=fixture_develop(1.25);before=fixture_develop(0);exact(d['expected_Develop'],expected)
 tags={'albums':['External'],'keywords':['incoming'],'label':'red','schema':1};entries=[{'action':'original','state':before},{'action':'xmp_merge','state':expected}]
 for row,cursor,state in [(d['stored_after'],1,expected),(d['stored_undo_row'],0,before)]:
  exact(row,{'Develop':state,'flag':'pick','rating':4,'tags':tags,'history':{'cursor':cursor,'entries':entries,'luts':[],'schema':1}})
 source=d['source_worker_events'];sql=d['SQL_commit_events'];gui=d['GUI_publication_events'];need(type(source)is list and len(source)==2 and type(sql)is list and len(sql)==1 and type(gui)is list and len(gui)==2,'actual producer chains')
 need([x['phase']for x in source]==['review','precommit']and [x['action']for x in source]==['xmp_merge_source_verified']*2,'two real complete original verification phases')
 need([x['action']for x in gui]==['xmp_merge_commit_queued','xmp_merge_durable']and sql[0]['action']=='xmp_merge_sql_committed'and sql[0]['record_valid_after_commit']is True,'queue/durable real SQL chain')
 first=source[0];roles=[d['GUI_thread'],d['afterFrameEnd_RHI_thread'],sql[0]['SQL_thread']]+[x['worker_thread']for x in source]
 need(all(type(x)is str and re.fullmatch('[0-9a-f]+',x)is not None for x in roles),'actual thread handles')
 need(len(set(roles[:3]))==3 and all(x not in roles[:3]for x in roles[3:]),'distinct GUI RHI sourceCPU SQL roles')
 for x in source+sql:
  for k in ['project','storage_key','controller','sequence','source_identity','sidecar_sha256']:need(type(x[k])is str and x[k]and x[k]==first[k],'actual current context '+k)
  need(x['source_identity']==d['original_source_SHA256']and x['sidecar_sha256']==d['sidecar_SHA256'],'source and captured sidecar binding');sequence(x['sequence'])
 need(re.fullmatch('[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}',first['controller'])is not None,'actual controller UUID')
 for x in source:
  for k in ['catalog_epoch','photo_epoch']:sequence(x[k]);need(x[k]==first[k],'current source epoch')
  need(type(x['original_bytes'])is str and re.fullmatch('[1-9][0-9]*',x['original_bytes'])is not None and 0<int(x['original_bytes'])<=2*1024**3,'original bounded complete read size');need(x['original_bytes']==first['original_bytes'],'same original size')
 for k in ['project','storage_key','sequence','sidecar_sha256']:need(gui[0][k]==first[k],'actual queued GUI context')
 need(gui[1]['photo_key']==first['storage_key']and gui[1]['sidecar_sha256']==first['sidecar_sha256']and all(gui[1][k]==first[k]for k in ['catalog_epoch','photo_epoch']),'actual final GUI context')
 times=[stamp(x['time'])for x in [source[0],source[1],gui[0],sql[0],gui[1]]];need(times==sorted(times),'actual source/precommit/queued/SQL durable/GUI ordering')
 captures=d['captures'];need(type(captures)is list and len(captures)==3 and [x['kind']for x in captures]==['review','applied','undo'],'three actual captures')
 need(len({x['path']for x in captures})==3,'unique owned captures')
 for x in captures:
  need(type(x['path'])is str and x['path'].endswith('.png'),'PNG capture locator');integer(x['width'],100,16384);integer(x['height'],80,16384);digest(x['sha256'])
 need((captures[1]['width'],captures[1]['height'])==(captures[2]['width'],captures[2]['height']),'applied and undo actual window geometry')
 return {'passed':True,'language':language,'GUI_ms':d['elapsed_ms'],'actual_qpa':d['actual_qpa'],'actual_backend':d['actual_scene_backend'],'actual_DPR':d['actual_DPR'],'source_worker_SQL_GUI_chain_bound':True,'full_six_field_record_Develop_undo_reopen_history_verified':True,'private_compiled_unknown_allowed_only_for_explicit_original_example':private_example and s=='unknown','QML_menu_pointer_or_new_RAW_or_installed_package_or_current_CI_claim':False}

def png(path,row):
 need(Path(path).stat().st_size<=BUDGET//3,'bounded owned UI PNG file, not RAW/export');data=Path(path).read_bytes();need(data[:8]==b'\x89PNG\r\n\x1a\n','PNG signature');at=8;image=None;parts=[];ended=False
 while at<len(data):
  need(at+12<=len(data),'complete PNG chunk');n=struct.unpack_from('>I',data,at)[0];need(at+n+12<=len(data),'bounded PNG chunk');kind=data[at+4:at+8];chunk=memoryview(data)[at+8:at+n+8];need(zlib.crc32(chunk,zlib.crc32(kind))&0xffffffff==struct.unpack_from('>I',data,at+n+8)[0],'PNG CRC')
  if kind==b'IHDR':need(image is None and n==13,'one IHDR');image=struct.unpack('>2I5B',chunk)
  elif kind==b'IDAT':parts.append(chunk)
  elif kind==b'IEND':need(n==0,'empty IEND');ended=True;at+=12;break
  at+=n+12
 need(ended and at==len(data) and image is not None,'complete exact PNG');w,h,depth,color,compression,filtering,interlace=image;need((w,h)==(row['width'],row['height'])and depth==8 and color in(2,6)and(compression,filtering,interlace)==(0,0,0),'actual PNG geometry/format')
 need(hashlib.sha256(data).hexdigest()==row['sha256'],'full PNG SHA256');integer(w,1,16384);integer(h,1,16384);bpp=3 if color==2 else 4;stride=w*bpp;expected=h*(stride+1);decoder=zlib.decompressobj();encoded=bytearray()
 # Bound each compressed feed, temporary output and the total declared row payload.
 for part in parts:
  for offset in range(0,len(part),65536):
   pending=part[offset:offset+65536]
   while pending:
    need(not decoder.eof,'no compressed data after completed stream');remaining=expected+1-len(encoded);need(remaining>0,'bounded declared PNG rows');chunk=decoder.decompress(pending,min(65536,remaining));encoded.extend(chunk);need(len(encoded)<=expected,'PNG data exceeds declared rows');pending=decoder.unconsumed_tail
 need(decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail and len(encoded)==expected,'complete compressed rows')
 previous=bytearray(stride);pixels=hashlib.sha256();counts=[0]*5
 for y in range(h):
  offset=y*(stride+1);kind=encoded[offset];need(kind<=4,'known PNG row filter');counts[kind]+=1;out=bytearray(encoded[offset+1:offset+1+stride])
  if kind==1:
   for x in range(bpp,stride):out[x]=(out[x]+out[x-bpp])&255
  elif kind==2:out=bytearray((a+b)&255 for a,b in zip(out,previous))
  elif kind in(3,4):
   for x in range(stride):
    left=out[x-bpp]if x>=bpp else 0;up=previous[x];corner=previous[x-bpp]if x>=bpp else 0
    if kind==3:prediction=(left+up)//2
    else:
     p=left+up-corner;a=abs(p-left);b=abs(p-up);c=abs(p-corner);prediction=left if a<=b and a<=c else up if b<=c else corner
    out[x]=(out[x]+prediction)&255
  pixels.update(out);previous=out
 return {'path':str(path),'sha256':row['sha256'],'width':w,'height':h,'bytes':len(data),'all_chunk_CRC_verified':True,'fully_decoded_rows':h,'all_pixels_RGBA_or_RGB_bytes':stride*h,'decoded_pixel_sha256':pixels.hexdigest(),'filter_row_counts':counts}
