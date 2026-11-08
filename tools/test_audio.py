"""Host checks for encoding quality, memory-source identity and packed ranges."""
import array
import contextlib
import hashlib
import io
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from psp_audio import audit_signatures, build_encoder, fingerprint, pack_audio
from psp_disc import path_hash
from psp_video import ffmpeg_path


class AudioTests(unittest.TestCase):
    def test_same_prefix_sparse_differences(self):
        a = bytes(4096)
        b = bytearray(a); b[2307] = 1
        c = bytearray(a); c[2341] = 2
        with tempfile.TemporaryDirectory() as tmp:
            iso = Path(tmp) / 'source.bin'
            iso.write_bytes(a + b + c + a)
            rows = [dict(offset=i*4096, size=4096, signature=fingerprint(a)) for i in range(4)]
            audit_signatures(rows, iso)
            self.assertEqual(rows[0]['signature2'], rows[3]['signature2'])
            self.assertEqual(len({r['signature2'] for r in rows}), 3)
            self.assertEqual(set(rows[0]['signature_offsets']), {2307, 2341})

    def test_pack_preserves_ids_and_deduplicates_payloads(self):
        with tempfile.TemporaryDirectory() as tmp:
            loose, packed = Path(tmp) / 'loose', Path(tmp) / 'packed'
            loose.mkdir()
            rows = []
            for i, data in enumerate([b'first stream', b'second stream', b'first stream']):
                key = path_hash(f'SOUND/TEST.AFS#{i}')
                row = dict(hash=key, signature=12, signature2=i, signature_offsets=[123, 456], size=999)
                rows.append(row)
                name = f'{key:08x}.p4a'
                (loose / name).write_bytes(data)
                (loose / (name + '.json')).write_text(json.dumps({'sha256': hashlib.sha256(data).hexdigest()}))
            with contextlib.redirect_stdout(io.StringIO()):
                pack_audio(rows, loose, packed)
            index = (packed / 'audio.idx').read_bytes()
            self.assertEqual(index[:4], b'P4I3')
            count, stride, blob_size = struct.unpack_from('<3I', index, 4)
            self.assertEqual((count, stride, blob_size), (3, 32, 12))
            entries = {r[0]: r for r in struct.iter_unpack('<8I', index[16:16+count*stride])}
            self.assertEqual(entries[rows[0]['hash']][4:6], entries[rows[2]['hash']][4:6])
            payload = (packed / 'AUDIO.0').read_bytes()
            for row, data in zip(rows, [b'first stream', b'second stream', b'first stream']):
                entry = entries[row['hash']]
                self.assertEqual(entry[5] % 64, 0)
                self.assertEqual(payload[entry[5]:entry[5]+len(data)], data)

    def test_vag_roundtrip_signal_quality(self):
        samples = array.array('h', [int(17000*math.sin(i*0.07)+3000*math.sin(i*0.23)) for i in range(10080)])
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / 'input.pcm').write_bytes(samples.tobytes())
            subprocess.run([str(build_encoder()), str(p / 'input.pcm'), str(p / 'encoded.bin'), '1'], check=True)
            blocks = (p / 'encoded.bin').read_bytes()
            self.assertEqual(len(blocks), len(samples)//28*16)
            for i in range(0, len(blocks)//16, 256):
                self.assertEqual(blocks[i*16] >> 4, 0)
            header = bytearray(48)
            header[:4] = b'VAGp'
            struct.pack_into('>I', header, 4, 0x20)
            struct.pack_into('>II', header, 12, len(blocks), 24000)
            (p / 'test.vag').write_bytes(header + blocks)
            subprocess.run([ffmpeg_path(), '-v', 'error', '-y', '-i', str(p / 'test.vag'),
                            '-f', 's16le', str(p / 'decoded.pcm')], check=True)
            decoded = array.array('h'); decoded.frombytes((p / 'decoded.pcm').read_bytes())
            self.assertGreaterEqual(len(decoded), len(samples))
            signal = sum(x*x for x in samples)
            error = sum((x-y)**2 for x, y in zip(samples, decoded))
            self.assertGreater(10*math.log10(signal/error), 25)


if __name__ == '__main__':
    unittest.main()
