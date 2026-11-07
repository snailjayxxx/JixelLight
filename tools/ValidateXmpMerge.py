"""Validate an owned bilingual XMP import in the real QML App, preserving raw evidence."""
from __future__ import annotations
import argparse,hashlib,json,os
from pathlib import Path
import shutil,subprocess,sys,tempfile,time,zlib
from NativeBrushEvidence import commit
from ValidateNativeBrush import backend_environment,QML_ERRORS
from XmpMergeEvidence import validate,png
LANGUAGES=('en_US','zh_CN')
def digest(path):
 with Path(path).open('rb')as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def command(binary,raw,language):
 if language not in LANGUAGES:raise ValueError('Unsupported XMP validation language')
 return [str(binary),'--xmp-merge-smoke-report',str(raw),'--xmp-merge-smoke-language',language]
def outputs(report):
 report=Path(report)
 if report.suffix.lower()!='.json':raise ValueError('XMP validation report must end in .json')
 return [report,*[Path(str(report)+x)for x in ['.raw.json','.stdout','.stderr','.capture-0.png','.capture-1.png','.capture-2.png']]]
def preflight(binary,home,report,language,expected_commit=None):
 command(binary,report,language)
 if expected_commit is not None:commit(expected_commit,expected_commit)
 binary=Path(binary).resolve(strict=True);home=Path(home).resolve();report=Path(report).absolute()
 if not binary.is_file():raise ValueError('App executable is required')
 paths=outputs(report)
 for f in paths:
  if f.is_symlink()or f.exists()or f.resolve()==binary:raise ValueError('Evidence output must be new and cannot replace an input')
 if home==binary or home==binary.parent or (home.exists()and(not home.is_dir()or any(home.iterdir()))):raise ValueError('Validation home must be a new or empty owned directory')
 return binary,home,report,paths

def validate_xmp_merge(binary,environment,home,report,language,*,expected_commit=None):
 binary,home,report,paths=preflight(binary,home,report,language,expected_commit);home.mkdir(parents=True,exist_ok=True);report.parent.mkdir(parents=True,exist_ok=True)
 started=time.monotonic();result={'mode':'xmp-merge-'+language,'language':language,'passed':False,'production_accepted':False,'actual_installed_package_or_current_CI_acceptance':False,'QML_menu_pointer_exercised':False,'annotations_undoable':False}
 with tempfile.TemporaryDirectory(prefix='xmp-App-',dir=home)as directory:
  private=Path(directory);raw=private/'raw.json';env,qpa,backend=backend_environment(environment,private,'xmp-merge')
  try:
   process=subprocess.run(command(binary,raw,language),env=env,cwd=private,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=70)
   for path,content in [(paths[2],process.stdout),(paths[3],process.stderr)]:
    with path.open('x',encoding='utf-8')as stream:stream.write(content)
   result['returncode']=process.returncode
   if raw.is_file():
    with paths[1].open('xb')as stream:stream.write(raw.read_bytes())
   if process.returncode!=0:raise ValueError('Normal XMP App process failed')
   if any(x in process.stdout+process.stderr for x in QML_ERRORS):raise ValueError('QML runtime error')
   data=json.loads(raw.read_text(encoding='utf-8'));proof=validate(data,language,expected_commit=expected_commit)
   if data['actual_qpa']!=qpa or data['actual_scene_backend']!=backend:raise ValueError('Actual App QPA/backend differs from required owner')
   captures=[]
   for i,row in enumerate(data['captures']):
    image=Path(row['path']);resolved=image.resolve(strict=True)
    if image.is_symlink()or not resolved.is_relative_to(private.resolve())or image.stat().st_nlink!=1:raise ValueError('Capture must belong only to this owned App run')
    evidence=png(image,row);destination=paths[4+i]
    with destination.open('xb')as stream:stream.write(image.read_bytes())
    if digest(destination)!=evidence['sha256']:raise ValueError('Capture copy differs')
    captures.append({**evidence,'path':str(destination)})
   elapsed=int((time.monotonic()-started)*1000)
   if elapsed>=120000:raise ValueError('XMP wrapper exceeded its120s total bound')
   result.update(passed=True,source_commit=data['source_commit'],actual_qpa=qpa,actual_backend=backend,actual_DPR=data['actual_DPR'],GUI_elapsed_ms=data['elapsed_ms'],GUI_deadline_ms=60000,wrapper_elapsed_ms=elapsed,wrapper_deadline_ms=120000,executable_sha256=digest(binary),raw_report={'path':str(paths[1]),'sha256':digest(paths[1])},strict_report=proof,captures=captures,full_six_fields_durable_reopen_DevelopUndo_history_verified=True,original_and_sidecar_unchanged=True,actual_GUI_RHI_sourceCPU_SQL_chain_verified=True,raw_report_preserved=True)
  except (AssertionError,ValueError,KeyError,TypeError,OSError,subprocess.TimeoutExpired,zlib.error)as error:result['error']=str(error)
 with report.open('x',encoding='utf-8')as stream:json.dump(result,stream,indent=2,allow_nan=False);stream.write('\n')
 return result

def main(argv=None):
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('executable',type=Path);parser.add_argument('report',type=Path);parser.add_argument('language',choices=LANGUAGES);parser.add_argument('--expected-commit');args=parser.parse_args(argv)
 try:
  with tempfile.TemporaryDirectory(prefix='jixellight-XMP-home-')as home:result=validate_xmp_merge(args.executable,os.environ,Path(home),args.report,args.language,expected_commit=args.expected_commit)
 except (OSError,ValueError,AssertionError)as error:print(str(error),file=sys.stderr);return 2
 print(json.dumps(result,indent=2,allow_nan=False));return 0 if result['passed']else 1
if __name__=='__main__':sys.exit(main())
