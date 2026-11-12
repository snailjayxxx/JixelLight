"""Independent forward-filter fixtures for the complete PNG byte-proof decoder."""
from pathlib import Path
import hashlib
import random
import struct
import sys
import tempfile
import unittest
import zlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ValidateRawPressureQpa import png


def chunk(kind, data):
    return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)


def encode(rows, filters, depth=8, color=6, profile=None, compressed=None, header=None):
    bpp=(3 if color==2 else 4)*(depth//8)
    previous=bytes(len(rows[0]));scan=bytearray()
    for row,kind in zip(rows,filters):
        scan.append(kind)
        for x,value in enumerate(row):
            a=row[x-bpp]if x>=bpp else 0;b=previous[x];c=previous[x-bpp]if x>=bpp else 0
            if kind==0:prediction=0
            elif kind==1:prediction=a
            elif kind==2:prediction=b
            elif kind==3:prediction=(a+b)//2
            else:
                p=a+b-c;distances=(abs(p-a),abs(p-b),abs(p-c));prediction=(a,b,c)[distances.index(min(distances))]
            scan.append((value-prediction)&255)
        previous=row
    ihdr=header or struct.pack('>2I5B',len(rows[0])//bpp,len(rows),depth,color,0,0,0)
    body=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',ihdr)
    if profile is not None:body+=chunk(b'iCCP',b'owned-test\0\0'+zlib.compress(profile))
    return body+chunk(b'IDAT',compressed if compressed is not None else zlib.compress(scan))+chunk(b'IEND',b'')


class TestPngUnfilter(unittest.TestCase):
    def read(self,data):
        with tempfile.TemporaryDirectory(prefix='png-unfilter-owned-')as temp:
            path=Path(temp)/'fixture.png';path.write_bytes(data);return png(path)

    def test_all_filters_rgb_rgba_and_both_depths_match_known_source_bytes(self):
        rng=random.Random(63903)
        for depth in (8,16):
            for color in (2,6):
                bpp=(3 if color==2 else 4)*(depth//8)
                for width in (1,2,3,17,257):
                    rows=[]
                    for y in range(15):
                        n=width*bpp
                        rows.append(bytes(n)if y%5==0 else bytes([255])*n if y%5==1 else bytes((0,255)[i%2]for i in range(n))if y%5==2 else bytes(rng.randrange(256)for _ in range(n)))
                    filters=list(range(5))*3
                    with self.subTest(depth=depth,color=color,width=width):
                        meta,actual=self.read(encode(rows,filters,depth,color));expected=b''.join(rows)
                        self.assertEqual(actual,expected);self.assertEqual(meta['filters'],[3]*5)
                        self.assertEqual(meta['full_unfiltered_rows'],len(rows));self.assertTrue(meta['CRC_all_chunks'])
                        native=bytearray(expected)
                        if depth==16 and sys.byteorder=='little':native[0::2],native[1::2]=expected[1::2],expected[0::2]
                        self.assertEqual(meta['decoded_native_pixel_sha256'],hashlib.sha256(native).hexdigest())

    def test_paeth_ties_keep_left_then_above_then_upper_left(self):
        # Unequal predictors at exact ties; wrong tie order changes output bytes.
        for a,b,c in ((68,158,128),(158,68,128),(255,0,0),(0,255,0),(255,255,255)):
            for depth,color in ((8,2),(8,6),(16,2),(16,6)):
                bpp=(3 if color==2 else 4)*(depth//8)
                rows=[bytes([c])*bpp+bytes([b])*bpp,bytes([a])*bpp+bytes([37])*bpp]
                with self.subTest(a=a,b=b,c=c,depth=depth,color=color):
                    self.assertEqual(self.read(encode(rows,[0,4],depth,color))[1],b''.join(rows))

    def test_full_width_prefix_and_up_carries_never_cross_byte_lanes(self):
        for depth,color in ((8,2),(8,6),(16,2),(16,6)):
            bpp=(3 if color==2 else 4)*(depth//8);width=8192
            first=bytes((255*(i//bpp+1)+i%bpp)&255 for i in range(width*bpp))
            second=bytes((v+255)&255 for v in first);rows=[first,second,bytes(len(first))]
            meta,actual=self.read(encode(rows,[1,2,2],depth,color));self.assertEqual(actual,b''.join(rows));self.assertEqual(meta['width'],8192)

    def test_icc_crc_shape_zlib_and_trailing_rejections_remain(self):
        rows=[bytes(range(24)),bytes(reversed(range(24)))];profile=bytearray(128);struct.pack_into('>I',profile,0,len(profile));profile[36:40]=b'acsp'
        valid=encode(rows,[1,2],profile=bytes(profile));meta,actual=self.read(valid)
        self.assertEqual(actual,b''.join(rows));self.assertEqual(meta['ICC_bytes'],128);self.assertEqual(meta['ICC_sha256'],hashlib.sha256(profile).hexdigest())
        corrupt=bytearray(valid);corrupt[-1]^=1
        invalid=[valid[:-1],valid+b'trailing',bytes(corrupt),encode(rows,[1,2],compressed=b'bad-deflate'),
                 encode(rows,[1,2],compressed=zlib.compress(b'\x05'+bytes(49))),
                 encode(rows,[1,2],compressed=zlib.compress(bytes(49))),
                 encode(rows,[1,2],compressed=zlib.compress(bytes(51))),
                 encode(rows,[1,2],compressed=zlib.compress(bytes(50))+b'other-stream')]
        for width,height,depth,color,interlace in ((8193,1,8,6,0),(8192,8192,16,6,0),(6,2,12,6,0),(6,2,8,0,0),(6,2,8,6,1)):
            invalid.append(encode(rows,[1,2],header=struct.pack('>2I5B',width,height,depth,color,0,0,interlace)))
        for i,data in enumerate(invalid):
            with self.subTest(case=i),self.assertRaises((AssertionError,zlib.error)):
                self.read(data)

if __name__=='__main__':unittest.main()
