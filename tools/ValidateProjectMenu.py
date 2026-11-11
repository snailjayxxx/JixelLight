"""Run the real owned four-case project menu mode; preserve raw streams and captures."""
from __future__ import annotations
import argparse,hashlib,json,os
from pathlib import Path
import shutil,subprocess,sys,tempfile,time,zlib
from NativeBrushEvidence import commit
from ProjectMenuEvidence import decode_report,validate
from ValidateNativeBrush import backend_environment,QML_ERRORS
from XmpMergeEvidence import png

def digest(path):
    with Path(path).open('rb')as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def command(binary,raw):return [str(binary),'--project-menu-layout-smoke-report',str(raw)]

def outputs(report):
    report=Path(report)
    if report.suffix.lower()!='.json':raise ValueError('Menu report must end in .json')
    return [report,*[Path(str(report)+s)for s in ['.raw.json','.stdout','.stderr',*(f'.case-{i}.png'for i in range(4))]]]

def preflight(binary,home,report,expected_commit=None):
    if expected_commit is not None:commit(expected_commit,expected_commit)
    binary=Path(binary).resolve(strict=True);home=Path(home).resolve();report=Path(report).absolute()
    if not binary.is_file():raise ValueError('App executable is required')
    paths=outputs(report)
    for path in paths:
        if path.exists()or path.is_symlink()or path.resolve()==binary:raise ValueError('Menu evidence must be new and cannot replace an input')
    if home==binary or home==binary.parent or(home.exists()and(not home.is_dir()or any(home.iterdir()))):raise ValueError('Menu home must be a new or empty owned directory')
    return binary,home,report,paths

def validate_project_menu(binary,environment,home,report,*,expected_commit=None):
    binary,home,report,paths=preflight(binary,home,report,expected_commit);home.mkdir(parents=True,exist_ok=True);report.parent.mkdir(parents=True,exist_ok=True)
    started=time.monotonic();result={'mode':'project-menu-layout','passed':False,'production_accepted':False,'actual_installed_package_or_current_CI_acceptance':False,'image_import_or_XMP_merge_exercised':False,'physical_input_or_complete_UI_claim':False}
    with tempfile.TemporaryDirectory(prefix='menu-App-',dir=home)as directory:
        private=Path(directory);raw=private/'raw.json';env,qpa,backend=backend_environment(environment,private,'project-menu-layout')
        def preserve(stdout,stderr):
            for path,content in [(paths[2],stdout),(paths[3],stderr)]:
                with path.open('xb')as stream:stream.write(content if content is not None else b'')
            result['original_process_streams_byte_exact']=True
            if raw.is_file():
                if raw.is_symlink()or raw.stat().st_nlink!=1:raise ValueError('Raw menu report must belong only to this owned run')
                with raw.open('rb')as source,paths[1].open('xb')as stream:shutil.copyfileobj(source,stream,64*1024)
                result['raw_report_preserved']=True
        try:
            try:process=subprocess.run(command(binary,raw),env=env,cwd=private,capture_output=True,timeout=70)
            except subprocess.TimeoutExpired as error:
                preserve(error.stdout,error.stderr);result['process_timed_out']=True;result['timeout_streams_are_partial']=True
                raise
            preserve(process.stdout,process.stderr)
            result['returncode']=process.returncode
            if process.returncode!=0:raise ValueError('Menu App process failed')
            if any(error in(process.stdout+process.stderr).decode('utf-8',errors='replace')for error in QML_ERRORS):raise ValueError('Menu QML runtime error')
            if paths[1].stat().st_size>2*1024*1024:raise ValueError('Menu metadata exceeds2MiB')
            frozen=paths[1].read_bytes();raw_sha=hashlib.sha256(frozen).hexdigest()
            data=decode_report(frozen.decode('utf-8'));proof=validate(data,expected_commit=expected_commit)
            if data['actual_qpa']!=qpa or data['actual_backend_name']!=backend:raise ValueError('Menu actual QPA/backend differs from required owner')
            captures=[]
            for i,row in enumerate(data['cases']):
                cap=row['capture'];image=Path(cap['path']);resolved=image.resolve(strict=True)
                if image.is_symlink()or not resolved.is_relative_to(private.resolve())or image.stat().st_nlink!=1 or image!=Path(str(raw)+f'.case-{i}.png'):raise ValueError('Menu capture must be the unique exact owned case output')
                evidence=png(image,cap);destination=paths[4+i]
                with image.open('rb')as source,destination.open('xb')as stream:shutil.copyfileobj(source,stream,64*1024)
                if digest(destination)!=evidence['sha256']:raise ValueError('Menu capture copy differs')
                captures.append({**evidence,'path':str(destination)})
            executable_sha=digest(binary)
            if digest(paths[1])!=raw_sha:raise ValueError('Frozen menu report changed during validation')
            elapsed=int((time.monotonic()-started)*1000)
            if elapsed>=120000:raise ValueError('Menu wrapper exceeded120s total bound')
            result.update(passed=True,source_commit=data['source_commit'],actual_qpa=qpa,actual_backend=backend,actual_DPR=data['actual_DPR'],GUI_elapsed_ms=data['GUI_elapsed_ms'],GUI_deadline_ms=60000,wrapper_elapsed_ms=elapsed,wrapper_deadline_ms=120000,executable_sha256=executable_sha,raw_report={'path':str(paths[1]),'sha256':raw_sha},strict_report=proof,captures=captures,independent_two_pointer_anchor_arithmetic=True,raw_report_preserved=True)
        except(AssertionError,ValueError,KeyError,TypeError,OSError,subprocess.TimeoutExpired,zlib.error)as error:result['error']=str(error)
    with report.open('x',encoding='utf-8')as stream:json.dump(result,stream,indent=2,allow_nan=False);stream.write('\n')
    return result

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('executable',type=Path);parser.add_argument('report',type=Path);parser.add_argument('--expected-commit');args=parser.parse_args(argv)
    try:
        with tempfile.TemporaryDirectory(prefix='jixellight-menu-home-')as home:result=validate_project_menu(args.executable,os.environ,Path(home),args.report,expected_commit=args.expected_commit)
    except(OSError,ValueError,AssertionError)as error:print(str(error),file=sys.stderr);return 2
    print(json.dumps(result,indent=2,allow_nan=False));return 0 if result['passed']else 1
if __name__=='__main__':sys.exit(main())
