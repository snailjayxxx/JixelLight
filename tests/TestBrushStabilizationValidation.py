"""Synthetic controls for the selected-rule validator, not actual UI acceptance."""
import copy
import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from ValidateBrushStabilization import (REQUIRED, FALSE_SCOPE, LABELS,
    validate_stabilization_report, validate_brush_stabilization, mouse_reference,
    strict_json, copy_captures, sha256)


def valid_report():
    data = {key: True for key in REQUIRED}
    data.update({key: False for key in FALSE_SCOPE})
    start, end = [.4,.6], [.4+.49/512,.6]
    reference = mouse_reference(start, end)
    data.update(passed=True,error="",source_kind="owned-rgba64-png",source_width=512,
        source_height=320,source_pixels=512*320,source_alpha_u16=65535,
        source_sha256="a"*64,source_commit="abcdef0",injection="Qt-QPA-controlled-pressure-stylus",
        window_focus_injection="Qt-QPA-controlled-window-focus",elapsed_ms=1234,deadline_ms=60000,
        dpr=1,actual_qpa="offscreen",gpu_active=False,backend="CPU parallel",mode="paint",language="en_US",
        single_stroke_dab_limit=4096,document_dab_limit=16384,selected_final_dabs=2,
        position_deadband_source_pixels=.5,selected_input_rule="half-image-pixel-anchor",
        selected_style={"radius":.04,"hardness":1,"opacity":.7},selected_dabs=[[.5,.6,.501],[.5,.6,0]],
        off_mode_first_refused_packet=8192,mouse_start_source=start,mouse_end_source=end,
        mouse_move_source_pixels=math.hypot((end[0]-start[0])*512,(end[1]-start[1])*320),
        mouse_actual_dabs=copy.deepcopy(reference),mouse_expected_dabs=reference,
        captures=[{"label":label,"path":"/synthetic/case.native.json."+label+"-owned.png",
                   "width":1180,"height":720,"dpr":1,"sha256":"b"*64} for label in sorted(LABELS)])
    return data


def png():
    def chunk(kind,data):
        return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data)&0xffffffff)
    return (b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",1,1,8,6,0,0,0))
            +chunk(b"IDAT",zlib.compress(b"\0\x80\x80\x80\xff"))+chunk(b"IEND",b""))


class StabilizationValidationTests(unittest.TestCase):
    def reject(self,data):
        with self.assertRaises((AssertionError,KeyError,ValueError,TypeError)):
            validate_stabilization_report(data,"cpu",False,False,"offscreen")

    def test_synthetic_positive_scope_and_real_input_rule(self):
        validate_stabilization_report(valid_report(),"cpu",False,False,"offscreen")

    def test_every_behavior_is_required(self):
        for key in REQUIRED:
            with self.subTest(key=key):
                d=valid_report();d[key]=False;self.reject(d)
                del d[key];self.reject(d)

    def test_unverified_claims_stay_false(self):
        for key in FALSE_SCOPE:
            d=valid_report();d[key]=True;self.reject(d)

    def test_original_capacity_and_deadline_preserved(self):
        for key,value in (("single_stroke_dab_limit",4097),("document_dab_limit",16385),
                          ("deadline_ms",60001),("elapsed_ms",60000),("elapsed_ms",True),
                          ("selected_final_dabs",3),("selected_final_dabs",True)):
            d=valid_report();d[key]=value;self.reject(d)

    def test_selected_rule_and_half_pixel_radius(self):
        for key,value in (("selected_input_rule","automatic-split"),("position_deadband_source_pixels",.51),
                          ("position_deadband_source_pixels",.49),("position_deadband_source_pixels",float("nan"))):
            d=valid_report();d[key]=value;self.reject(d)

    def test_selected_peak_and_center_exact(self):
        mutations=(lambda d:d["selected_dabs"][0].__setitem__(2,.502),
                   lambda d:d["selected_dabs"][1].__setitem__(0,.5+1e-15),
                   lambda d:d["selected_dabs"][1].__setitem__(2,.1),
                   lambda d:d["selected_dabs"][0].__setitem__(0,float("nan")),
                   lambda d:d["selected_dabs"].append([.5,.6,0]),
                   lambda d:d["selected_style"].update(hardness=True),
                   lambda d:d["selected_style"].update(extra=1))
        for mutate in mutations:
            d=valid_report();mutate(d);self.reject(d)

    def test_raw_mouse_uses_independent_unfiltered_sampler(self):
        d=valid_report();d["mouse_actual_dabs"][0][0]+=.00001;self.reject(d)
        d=valid_report();d["mouse_expected_dabs"][0][0]+=.00001;d["mouse_actual_dabs"]=copy.deepcopy(d["mouse_expected_dabs"]);self.reject(d)
        d=valid_report();d["mouse_expected_dabs"]=[[.4,.6,1]];d["mouse_actual_dabs"]=[[.4,.6,1]];self.reject(d)
        d=valid_report();d["mouse_expected_dabs"][0][2]=True;d["mouse_actual_dabs"][0][2]=True;self.reject(d)

    def test_mouse_move_requires_nonzero_bounded_displacement(self):
        for value in (0,-.1,.51,float("inf"),float("nan")):
            d=valid_report();d["mouse_move_source_pixels"]=value;self.reject(d)
        d=valid_report();d["mouse_end_source"]=d["mouse_start_source"][:];self.reject(d)

    def test_raw_mode_still_reaches_existing_refusal(self):
        for value in (0,7999,9001,True):
            d=valid_report();d["off_mode_first_refused_packet"]=value;self.reject(d)

    def test_owned_source_and_mode_language_backend(self):
        for key,value in (("source_alpha_u16",42000),("source_width",True),("source_pixels",0),
                          ("source_commit","unknown"),("mode","erase"),("language","zh_CN"),
                          ("gpu_active",True),("backend","GPU"),("injection","direct-controller")):
            d=valid_report();d[key]=value;self.reject(d)

    def test_qpa_dpr_and_finite_numbers(self):
        for key,value in (("dpr",0),("dpr",float("inf")),("dpr",True),("actual_qpa","windows"),
                          ("source_sha256","bad")):
            d=valid_report();d[key]=value;self.reject(d)

    def test_strict_json_rejects_duplicate_and_nonfinite(self):
        for text in ('{"a":1,"a":2}','{"a":{"b":1,"b":2}}','{"a":NaN}','{"a":Infinity}','{"a":1e999}'):
            with self.assertRaises(ValueError):strict_json(text)

    def capture_case(self,mutate=None,label="selected"):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);private=root/"private";private.mkdir();published=root/"published";published.mkdir()
            native=private/"case.native.json";path=Path(str(native)+"."+label+"-owned.png");path.write_bytes(png())
            data={"passed":False,"dpr":1,"captures":[{"label":label,"path":str(path),"width":1,"height":1,"dpr":1,"sha256":sha256(path)}]}
            if mutate:mutate(data,path,published)
            return copy_captures(data,native,published/"case.json")

    def test_capture_partial_and_failure_are_preserved(self):
        for label in ("selected","failure"):
            copied,errors=self.capture_case(label=label);self.assertEqual(set(copied),{label});self.assertEqual(errors,[]);self.assertNotEqual(set(copied),LABELS)

    def test_capture_corrupt_png_and_hash_are_rejected(self):
        def corrupt(d,p,o):
            data=bytearray(p.read_bytes());data[20]^=1;p.write_bytes(data);d["captures"][0]["sha256"]=sha256(p)
        for change in (corrupt,lambda d,p,o:d["captures"][0].update(sha256="0"*64)):
            copied,errors=self.capture_case(change);self.assertFalse(copied);self.assertTrue(errors)

    def test_capture_existing_outputs_remain_unchanged(self):
        def occupied(d,p,o):(o/p.name).write_bytes(b"user-owned")
        copied,errors=self.capture_case(occupied);self.assertFalse(copied);self.assertTrue(errors)

    def test_capture_unknown_duplicate_outside_paths_rejected(self):
        for change in (lambda d,p,o:d["captures"][0].update(label="other"),
                       lambda d,p,o:d["captures"].append(copy.deepcopy(d["captures"][0])),
                       lambda d,p,o:d["captures"][0].update(path=str(p.parent/"other.png"))):
            copied,errors=self.capture_case(change);self.assertTrue(errors)

    def test_wrapper_existing_report_is_refused_without_launch(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);binary=root/"binary";binary.write_bytes(b"synthetic");report=root/"case.json";report.write_bytes(b"user-owned")
            with patch("ValidateBrushStabilization.subprocess.run",side_effect=AssertionError("must not launch")):
                with self.assertRaises(ValueError):validate_brush_stabilization(binary,{},root/"home",report,"cpu",qpa="offscreen")
            self.assertEqual(report.read_bytes(),b"user-owned")

    def test_failed_report_and_bad_capture_metadata_are_rejected(self):
        d=valid_report();d["passed"]=False;self.reject(d)
        d=valid_report();d["error"]="unfinished";self.reject(d)
        d=valid_report();d["captures"].pop();self.reject(d)
        d=valid_report();d["captures"][0]["width"]=1;self.reject(d)
        d=valid_report();d["captures"][0]["dpr"]=2;self.reject(d)
        d=valid_report();d["captures"][0]["sha256"]="bad";self.reject(d)


if __name__=="__main__":unittest.main()
