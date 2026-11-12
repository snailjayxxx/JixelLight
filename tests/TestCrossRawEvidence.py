import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from FetchCrossRawFixtures import read_manifest, fetch
from ValidateCrossRaw import validate_report, verify_pixels, run
from ValidateCli import chunk

MANIFEST = Path(__file__).parent / 'fixtures/cross-raw.json'


def report():
    spec = read_manifest(MANIFEST)
    cases = []
    for pinned in spec['items']:
        renders = []
        for index, suffix in enumerate(('.zero', '.ev075')):
            stem = pinned['id'] + suffix
            renders.append(dict(exposure=(0, .75)[index], raw_base_exposure_stops=0,
                                CPU_pixels_sha256=str(index+1)*64, GPU_pixels_sha256='3'*64, ICC_sha256='4'*64,
                                maximum_GPU_error_U16=0, actual_compute=True, CPU_reference=stem+'.cpu.rgba16',
                                GPU_readback=stem+'.gpu.rgba16', PNG16=stem+'.png'))
        cases.append(dict(id=pinned['id'], source_sha256=pinned['sha256'], model=pinned['model'], decoded_format='RGBA64',
                          linear_working_space='Linear ProPhoto RGB', source_alpha_exact=True, source_unchanged=True,
                          width=6000, height=4000, decoded_pixels_sha256='5'*64,
                          sensor_audit=dict(available=True, cfa_kind=pinned['cfa'], rgb_demosaiced=False, black_subtracted=False),
                          demosaic='X-Trans (LibRaw quality 3)' if pinned['cfa']=='xtrans_6x6' else 'AHD',
                          sample_width=128, sample_height=96, linear_sample=pinned['id']+'.linear.rgba16', renders=renders))
    return dict(schema=1, passed=True, error='', engine='jixellight-linear-v5-base2-look4', source_commit='a'*7,
                GPU_required=True, backend='owned-test-backend', camera_color_calibration_accepted=False,
                native_float_RAW_decoder=False, photographs_published=False, cases=cases)


class CrossRawEvidenceTests(unittest.TestCase):
    def test_three_formats_and_explicit_claim_boundaries(self):
        validate_report(report(), read_manifest(MANIFEST), 'a'*40)

    def test_missing_gpu_wrong_engine_and_inflated_claims_reject(self):
        for key, value in [('passed', False), ('GPU_required', False), ('backend', 'unavailable'),
                           ('engine', 'other'), ('camera_color_calibration_accepted', True),
                           ('native_float_RAW_decoder', True), ('photographs_published', True)]:
            data=report();data[key]=value
            with self.assertRaises(ValueError):validate_report(data,read_manifest(MANIFEST))
        with self.assertRaises(ValueError):validate_report(report(),read_manifest(MANIFEST),'b'*40)

    def test_camera_source_cfa_and_demosaic_cannot_be_substituted(self):
        for key,value in [('id','other'),('source_sha256','0'*64),('model','other'),('decoded_format','RGBA32FPx4'),
                          ('demosaic','AHD'),('source_alpha_exact',False),('source_unchanged',False)]:
            data=report();data['cases'][2][key]=value
            with self.assertRaises(ValueError):validate_report(data,read_manifest(MANIFEST))
        data=report();data['cases'][2]['sensor_audit']['cfa_kind']='bayer_like_filters'
        with self.assertRaises(ValueError):validate_report(data,read_manifest(MANIFEST))

    def test_hidden_base_gain_tolerance_and_output_escape_reject(self):
        for key,value in [('raw_base_exposure_stops',2.5),('exposure',True),('maximum_GPU_error_U16',41),
                          ('maximum_GPU_error_U16',False),('actual_compute',False),('GPU_readback','../other'),
                          ('CPU_reference','../other'),('PNG16','../other')]:
            data=report();data['cases'][0]['renders'][0][key]=value
            with self.assertRaises(ValueError):validate_report(data,read_manifest(MANIFEST))

    def owned_pixels(self,root):
        data=report();icc=b'owned-profile-bytes'
        for case in data['cases']:
            (root/case['linear_sample']).write_bytes(b'\0'*(128*96*8))
            for index,render in enumerate(case['renders']):
                pixel=struct.pack('>4H',1000+index,2000,3000,65535);pixels=pixel*(128*96)
                little=struct.pack('<4H',1000+index,2000,3000,65535)*(128*96)
                for key in ['CPU_reference','GPU_readback']:(root/render[key]).write_bytes(little)
                png=(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',128,96,16,6,0,0,0))
                     +chunk(b'iCCP',b'owned\0\0'+zlib.compress(icc))+chunk(b'IDAT',zlib.compress((b'\0'+pixel*128)*96))+chunk(b'IEND',b''))
                (root/render['PNG16']).write_bytes(png)
                render['CPU_pixels_sha256']=render['GPU_pixels_sha256']=hashlib.sha256(little).hexdigest()
                render['ICC_sha256']=hashlib.sha256(icc).hexdigest()
        return data

    def test_independent_png_and_all_gpu_pixels_are_checked(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);data=self.owned_pixels(root)
            self.assertEqual(sum(p['rows']for p in verify_pixels(data,root)),576)
            render=data['cases'][0]['renders'][0];path=root/render['GPU_readback'];pixels=bytearray(path.read_bytes());pixels[0:2]=struct.pack('<H',1041);path.write_bytes(pixels)
            with self.assertRaises(ValueError):verify_pixels(data,root)

    def test_pinned_manifest_rejects_urls_licenses_and_path_traversal(self):
        spec=read_manifest(MANIFEST)
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'manifest.json'
            for key,value in [('source','file:///etc/passwd'),('source','https://example.com/other'),
                              ('license','other'),('file','../original.NEF'),('bytes',True),('sha256','bad')]:
                bad=copy.deepcopy(spec);bad['items'][0][key]=value;path.write_text(json.dumps(bad))
                with self.assertRaises(ValueError):read_manifest(path)

    def test_cached_mismatch_never_downloads_or_overwrites(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);spec=read_manifest(MANIFEST);target=root/spec['items'][0]['file'];target.write_bytes(b'original failed fixture')
            with mock.patch('urllib.request.urlopen')as network:
                with self.assertRaises(ValueError):fetch(MANIFEST,root)
                network.assert_not_called()
            self.assertEqual(target.read_bytes(),b'original failed fixture')

    def test_occupied_evidence_namespace_never_launches_or_replaces(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);original=root/'cross.native.json';original.write_bytes(b'original failed evidence')
            with mock.patch('subprocess.run')as launch:
                with self.assertRaises(ValueError):run(root/'unused',MANIFEST,root,root/'cross.json')
                launch.assert_not_called()
            self.assertEqual(original.read_bytes(),b'original failed evidence')


if __name__=='__main__':unittest.main()
