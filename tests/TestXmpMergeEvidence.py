"""New wrapper dispatch/output isolation controls, no App/backend/package acceptance."""
from pathlib import Path
import os,sys,tempfile,unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import ValidateXmpMerge as x
class XmpWrapperControls(unittest.TestCase):
 def test_exact_language_arguments_and_no_external_source(self):
  for language in x.LANGUAGES:self.assertEqual(x.command('app','report.json',language),['app','--xmp-merge-smoke-report','report.json','--xmp-merge-smoke-language',language])
 def test_invalid_language_refuses_before_files_or_process(self):
  for value in ['', 'en', 'zh',True]:
   with self.assertRaises(ValueError):x.command('app','r.json',value)
 def test_output_plan_is_exact_unique_and_keeps_raw(self):
  rows=x.outputs(Path('/owned/report.json'));self.assertEqual(len(rows),7);self.assertEqual(len(set(rows)),7);self.assertEqual(rows[1],Path('/owned/report.json.raw.json'));self.assertEqual(rows[-1],Path('/owned/report.json.capture-2.png'))
 def test_wrong_suffix_refuses(self):
  for name in ['photo.arw','photo','report.png','report.json.arw']:
   with self.assertRaises(ValueError):x.outputs(Path(name))
 def test_existing_file_symlink_hardlink_cannot_be_evidence(self):
  with tempfile.TemporaryDirectory()as temp:
   p=Path(temp);exe=p/'app';exe.write_bytes(b'owned executable');home=p/'home'
   for kind in ['file','symlink','hardlink']:
    out=p/(kind+'.json')
    if kind=='file':out.write_bytes(b'owned report')
    elif kind=='symlink':out.symlink_to(exe)
    else:os.link(exe,out)
    with self.assertRaises(ValueError):x.preflight(exe,home,out,'en_US')
    self.assertEqual(exe.read_bytes(),b'owned executable');self.assertFalse(home.exists())
 def test_nonempty_home_refuses_before_process(self):
  with tempfile.TemporaryDirectory()as temp:
   p=Path(temp);exe=p/'app';exe.write_bytes(b'app');home=p/'home';home.mkdir();(home/'settings').write_bytes(b'keep')
   with self.assertRaises(ValueError):x.preflight(exe,home,p/'r.json','en_US')
   self.assertEqual((home/'settings').read_bytes(),b'keep')
 def test_environment_isolation_all_platforms_without_Sony_scale(self):
  for platform,qpa,backend in [('darwin','cocoa','Metal'),('win32','windows','Direct3D 11'),('linux','xcb','OpenGL')]:
   env,a,b=x.backend_environment({'QT_SCALE_FACTOR':'.5','JIXELLIGHT_FORCE_CPU':'1','JIXELLIGHT_PRIVATE_QML_ROOT':'unsafe','CUSTOM':'keep'},Path('/owned'), 'xmp-merge',platform)
   self.assertEqual((a,b),(qpa,backend));self.assertEqual(env['QT_QPA_PLATFORM'],qpa);self.assertEqual(env['QSG_RENDER_LOOP'],'threaded');self.assertEqual(env['HOME'],str(Path('/owned')));self.assertNotIn('QT_SCALE_FACTOR',env);self.assertNotIn('JIXELLIGHT_FORCE_CPU',env);self.assertNotIn('JIXELLIGHT_PRIVATE_QML_ROOT',env);self.assertEqual(env['CUSTOM'],'keep')
 def test_malformed_expected_commit_refuses_before_outputs(self):
  with tempfile.TemporaryDirectory()as temp:
   p=Path(temp);exe=p/'app';exe.write_bytes(b'app')
   for value in ['unknown','bad!',True]:
    with self.assertRaises((ValueError,AssertionError,TypeError)):x.preflight(exe,p/'home',p/'r.json','en_US',value)
   self.assertFalse((p/'home').exists())
 def test_CLI_routes_language_expected_commit_and_returncode(self):
  for language in x.LANGUAGES:
   with patch.object(x,'validate_xmp_merge',return_value={'passed':True})as validate:
    self.assertEqual(x.main(['app','result.json',language,'--expected-commit','1234567']),0);self.assertEqual(validate.call_args.args[4],language);self.assertEqual(validate.call_args.kwargs,{'expected_commit':'1234567'})
class NewBoundedPNGControls(unittest.TestCase):
 def image(self,payload,raw=None):
  import hashlib,struct,zlib
  def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
  data=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',2,2,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(payload)if raw is None else raw)+chunk(b'IEND',b'')
  return data,{'width':2,'height':2,'sha256':hashlib.sha256(data).hexdigest()}
 def test_bounded_valid_full_row_decode(self):
  import XmpMergeEvidence as m
  with tempfile.TemporaryDirectory()as temp:
   p=Path(temp)/'a.png';data,row=self.image(bytes([0,1,2,3,4,5,6])*2);p.write_bytes(data);v=m.png(p,row);self.assertEqual(v['fully_decoded_rows'],2);self.assertEqual(v['all_pixels_RGBA_or_RGB_bytes'],12)
 def test_deflate_excess_declared_rows_refused(self):
  import XmpMergeEvidence as m
  with tempfile.TemporaryDirectory()as temp:
   p=Path(temp)/'a.png';data,row=self.image(bytes([0])*65536);p.write_bytes(data)
   with self.assertRaises(ValueError):m.png(p,row)
 def test_valid_CRC_bad_deflate_refused(self):
  import XmpMergeEvidence as m,zlib
  with tempfile.TemporaryDirectory()as temp:
   p=Path(temp)/'a.png';data,row=self.image(b'',raw=b'invalid deflate');p.write_bytes(data)
   with self.assertRaises(zlib.error):m.png(p,row)
 def test_wrapper_deflate_failure_preserves_raw_and_failed_report(self):
  import json,zlib
  with tempfile.TemporaryDirectory()as temp:
   p=Path(temp);exe=p/'app';exe.write_bytes(b'owned stub');report=p/'report.json'
   def run(args,**kwargs):
    from types import SimpleNamespace
    raw=Path(args[2]);image=raw.parent/'review.png';image.write_bytes(b'invalid');env,qpa,backend=x.backend_environment({},raw.parent,'xmp-merge');raw.write_text(json.dumps({'actual_qpa':qpa,'actual_scene_backend':backend,'captures':[{'path':str(image)}]}));return SimpleNamespace(returncode=0,stdout='owned transport stub',stderr='')
   with patch.object(x.subprocess,'run',side_effect=run),patch.object(x,'validate',return_value={'passed':True}),patch.object(x,'png',side_effect=zlib.error('owned malformed PNG')):
    result=x.validate_xmp_merge(exe,{},p/'home',report,'en_US')
   self.assertFalse(result['passed']);self.assertIn('malformed PNG',result['error']);self.assertTrue(Path(str(report)+'.raw.json').is_file());self.assertTrue(report.is_file())
if __name__=='__main__':unittest.main()
