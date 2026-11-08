"""Movie container round trips against FFmpeg, including the optional 15 FPS mode.

python -m unittest discover -s tools -p test_video.py
"""
from pathlib import Path
import struct
import tempfile
import unittest

import av
from psp_video import HEADER, RECORD, avcc_parameters, ffmpeg_path, pack_video, run, detect_crop


def decode_p4v(path):
    """Decode the actual packed AVCC packets, independently of MP4 demuxing."""
    with path.open('rb') as f:
        header = HEADER.unpack(f.read(HEADER.size))
        magic, version, width, height, num, den, count, maximum, nsps, npps, *_ = header
        if magic != b'P4V1' or version != 1:
            raise ValueError('bad header')
        sps, pps = f.read(nsps), f.read(npps)
        extradata = (bytes([1, sps[1], sps[2], sps[3], 255, 225]) +
                     struct.pack('>H', nsps) + sps + b'\x01' + struct.pack('>H', npps) + pps)
        decoder = av.CodecContext.create('h264', 'r')
        decoder.extradata = extradata
        decoded = []
        timestamps = []
        for _ in range(count):
            size, pts = RECORD.unpack(f.read(RECORD.size))
            if not 0 < size <= maximum:
                raise ValueError('bad size')
            data = f.read(size)
            if len(data) != size:
                raise ValueError('short packet')
            decoded.extend(decoder.decode(av.Packet(data)))
            timestamps.append(pts)
        decoded.extend(decoder.decode(None))
        if f.read(1):
            raise ValueError('trailing bytes')
    return decoded, timestamps, (width, height, num, den)


class VideoTest(unittest.TestCase):
    def test_letterbox_fading_to_white_is_still_cropped(self):
        with tempfile.TemporaryDirectory() as d:
            source = Path(d) / 'fade.mp4'
            run([ffmpeg_path(), '-hide_banner', '-loglevel', 'error', '-y',
                 '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=30', '-t', '1',
                 '-vf', 'pad=640:448:160:134,fade=t=out:st=0.5:d=0.4:color=white',
                 '-c:v', 'libx264', '-crf', '0', source])
            rect, frames = detect_crop(ffmpeg_path(), source, 640, 448)
            self.assertEqual(rect, (158, 132, 324, 184))
            # Fully uniform white frames have no content bounding box.
            self.assertGreaterEqual(frames, 25)

    def test_crop_scans_past_inset_scene_and_preserves_later_edge_titles(self):
        with tempfile.TemporaryDirectory() as d:
            source = Path(d) / 'borders.mp4'
            # First half: inset picture. Second half: a bright title along the
            # lower edge. Cropping only early frames would destroy that title.
            run([ffmpeg_path(), '-hide_banner', '-loglevel', 'error', '-y',
                 '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=30', '-t', '1',
                 '-vf', "pad=640:448:160:134,drawbox=x=0:y=430:w=640:h=8:color=white:t=fill:enable='gte(t,0.5)'",
                 '-c:v', 'libx264', '-crf', '0', source])
            (x, y, w, h), frames = detect_crop(ffmpeg_path(), source, 640, 448)
            self.assertEqual((x, w), (0, 640))
            self.assertGreaterEqual(y + h, 438)
            self.assertLessEqual(y, 134)
            self.assertGreaterEqual(frames, 30)

    def test_round_trip_both_frame_rates(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            for fps, expected in [('30000/1001', 30), ('15000/1001', 15)]:
                with self.subTest(fps=fps):
                    mp4 = d / 'test.mp4'
                    run([ffmpeg_path(), '-hide_banner', '-loglevel', 'error', '-y',
                         '-f', 'lavfi', '-i', 'testsrc2=size=480x272:rate=' + fps,
                         '-t', '1', '-c:v', 'libx264', '-profile:v', 'baseline',
                         '-bf', '0', '-refs', '1', '-pix_fmt', 'yuv420p', mp4])
                    target = d / 'test.p4v'
                    info = pack_video(mp4, target, d / 'missing.pcm')
                    frames, pts, shape = decode_p4v(target)
                    self.assertEqual(len(frames), expected)
                    self.assertEqual(info['frames'], expected)
                    self.assertEqual(info['audio_samples'], 0)
                    self.assertEqual(shape[:2], (480, 272))
                    self.assertEqual(pts[0], 0)
                    step = 3003 if expected == 30 else 6006
                    self.assertEqual(pts, [i * step for i in range(expected)])
                    self.assertTrue(all((f.width, f.height, f.format.name) ==
                                        (480, 272, 'yuv420p') for f in frames))

    def test_reject_reordered_frames(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            mp4 = d / 'reordered.mp4'
            run([ffmpeg_path(), '-hide_banner', '-loglevel', 'error', '-y',
                 '-f', 'lavfi', '-i', 'testsrc2=size=480x272:rate=30',
                 '-t', '1', '-c:v', 'libx264', '-bf', '2', mp4])
            with self.assertRaisesRegex(ValueError, 'Reordered'):
                pack_video(mp4, d / 'bad.p4v', d / 'none.pcm')

    def test_bad_avcc(self):
        for data in [b'', bytes(8), b'\x01\x42\x00\x1e\xfe\xe1\x00',
                     b'\x01\x42\x00\x1e\xff\xe1\x00',
                     b'\x01\x42\x00\x1e\xff\xe1\x00\x08\x67']:
            with self.subTest(data=data), self.assertRaises(ValueError):
                avcc_parameters(data)


if __name__ == '__main__':
    unittest.main()
