import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

source = Path(__file__).with_name('qa12_strict_release_guard.py')
spec = importlib.util.spec_from_file_location('qa12', source)
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)


def dig(data):
    return hashlib.sha256(data).hexdigest()

class QA12Test(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.r=Path(self.tmp.name)/'release'
        self.base=self.r/'UPDATE/PSP/GAME/P4'
        self.base.mkdir(parents=True)
        self.b=249
        self.stems=[f'MOVIE{i:02d}' for i in range(23)]
        self.files={'EBOOT.PBP':b'code','p4data.bin':b'data','p4str.bin':b'str','SOUND/audio_manifest.json':b'{"files":{}}'}
        self.files.update({f'VIDEO/{x}.p4v':b'frame'+bytes([i]) for i,x in enumerate(self.stems)})
        for name,data in self.files.items():
            p=self.base/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        self.m={'build':self.b,'release':True,'update_root':'UPDATE/PSP/GAME/P4','movies':23,
                'audio_streams':0, 'files':{k:dig(v) for k,v in self.files.items()},
                'bytes':sum(len(v) for v in self.files.values())}
        self.trusted=Path(self.tmp.name)/'outside/manifest.json'
        self.trusted.parent.mkdir()
        self.orig={'build':self.b,'release':True,'artifact_sha256':{k:dig(self.files[k]) for k in g.TRIPLET}}
        self.movies = Path(self.tmp.name)/'outside/movies.json'
        self.movies.write_text(json.dumps(self.stems))
        self.movie_sha = g.sha256(self.movies)
        self.update()
    def update(self):
        (self.r/'release.json').write_text(json.dumps(self.m))
        self.trusted.write_text(json.dumps(self.orig))
        self.frozen = g.sha256(self.trusted)
    def result(self, **kwargs):
        return g.inspect(kwargs.get('release',self.r), self.trusted,
                         kwargs.get('digest',self.frozen), kwargs.get('build',self.b),
                         kwargs.get('stems',self.stems))
    def test_01_clean(self):self.assertEqual(self.result(),[])
    def test_02_delete_manifest_eboot(self):
        self.m['files'].pop('EBOOT.PBP');(self.base/'EBOOT.PBP').unlink();self.update()
        self.assertTrue(any(x.startswith('MISSING_REQUIRED') for x in self.result()))
    def test_03_delete_manifest_strings(self):
        self.m['files'].pop('p4str.bin');(self.base/'p4str.bin').unlink();self.update()
        self.assertTrue(any(x.startswith('MISSING_REQUIRED') for x in self.result()))
    def test_04_wrong_build(self):
        self.m['build']=250;self.update();self.assertIn('WRONG_BUILD',self.result())
    def test_05_wrong_bytes(self):
        self.m['bytes']=1;self.update();self.assertIn('BYTES_MISMATCH',self.result())
    def test_06_wrong_movie_stem(self):
        old=self.base/'VIDEO/MOVIE00.p4v';new=self.base/'VIDEO/HIDDEN.p4v';old.rename(new)
        self.m['files']['VIDEO/HIDDEN.p4v']=self.m['files'].pop('VIDEO/MOVIE00.p4v');self.update()
        self.assertIn('MOVIE_STEM_ID_MISMATCH',self.result())
    def test_07_mutated_binary_plus_modified_release_hash(self):
        (self.base/'EBOOT.PBP').write_bytes(b'mutated');self.m['files']['EBOOT.PBP']=dig(b'mutated');self.update()
        self.assertIn('INDEPENDENT_TRIPLET_MISMATCH:EBOOT.PBP',self.result())
    def test_08_stale_extra_file(self):
        (self.base/'VIDEO/retired.p4v').write_bytes(b'old');self.assertIn('UNDECLARED:VIDEO/retired.p4v',self.result())
    def test_09_trusted_manifest_modified(self):
        self.trusted.write_text('{}');self.assertIn('TRUST_REFERENCE_SHA_MISMATCH',self.result())
    def test_10_duplicate_json_key(self):
        (self.r/'release.json').write_text((self.r/'release.json').read_text().replace('"movies": 23','"movies": 23, "movies": 23'))
        self.assertTrue(any(x.startswith('INVALID_JSON') for x in self.result()))
    def test_11_symlink_root(self):
        alternate=Path(self.tmp.name)/'different';alternate.mkdir()
        # Test presence of a directory symlink along supplied UPDATE chain
        (self.r/'UPDATE/PSP/GAME/P4').rename(alternate/'P4')
        (self.r/'UPDATE/PSP/GAME/P4').symlink_to(alternate/'P4',target_is_directory=True)
        self.assertTrue(any(x.startswith('SYMLINK_IN_ROOT') for x in self.result()))
    def test_12_missing_audio_manifest(self):
        self.m['files'].pop('SOUND/audio_manifest.json');(self.base/'SOUND/audio_manifest.json').unlink();self.update()
        self.assertTrue(any(x.startswith('MISSING_REQUIRED') for x in self.result()))
    def test_13_no_canonical_catalogue(self):self.assertIn('UNTRUSTED_MOVIE_CATALOGUE',self.result(stems=[]))
    def test_14_wrong_expected_build(self):self.assertIn('WRONG_BUILD',self.result(build=250))
    def test_15_tamper_triplet_manifests(self):
        self.orig['artifact_sha256']['p4str.bin']='0'*64;self.update()
        self.assertIn('INDEPENDENT_TRIPLET_MISMATCH:p4str.bin',self.result())
    def test_16_nested_traversal(self):
        self.m['files']['../evil.bin']=dig(b'x');self.update()
        self.assertIn('UNSAFE_KEY:../evil.bin',self.result())
    def test_17_missing_file_not_in_manifest_but_forged_count(self):
        (self.base/'VIDEO/MOVIE00.p4v').unlink();self.m['files'].pop('VIDEO/MOVIE00.p4v');self.update()
        self.assertIn('MOVIE_STEM_ID_MISMATCH',self.result())
    def test_18_no_trust_hash(self):self.assertIn('BAD_TRUST_DIGEST',self.result(digest=''))
    def test_19_catalogue_tamper_digest(self):
        self.movies.write_text('[]')
        self.assertIn('TRUST_REFERENCE_SHA_MISMATCH',g.verify_reference(self.movies,self.movie_sha,self.r))
    def test_20_catalogue_inside_release(self):
        self.movies.rename(self.r/'films.json')
        self.assertIn('TRUST_REFERENCE_INSIDE_RELEASE_TREE',g.verify_reference(self.r/'films.json',g.sha256(self.r/'films.json'),self.r))
    def test_21_cli_known_good_fixture(self):
        self.assertEqual(g.main(['--release-folder',str(self.r),'--trusted-manifest',str(self.trusted),
            '--trusted-manifest-sha256',self.frozen,'--expected-build',str(self.b),
            '--original-movie-stems',str(self.movies),'--original-movie-stems-sha256',self.movie_sha]),0)
    def test_22_cli_wrong_stem_pin(self):
        self.assertEqual(g.main(['--release-folder',str(self.r),'--trusted-manifest',str(self.trusted),
            '--trusted-manifest-sha256',self.frozen,'--expected-build',str(self.b),
            '--original-movie-stems',str(self.movies),'--original-movie-stems-sha256','0'*64]),2)

if __name__=='__main__':unittest.main(verbosity=2)
