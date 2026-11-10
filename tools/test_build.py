"""Source-build integrity, cache recovery and launch-only installation checks."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import psp_build as builder
from psp_prepare import prepare, _hide_child_windows
from psp_prepare_release import copy_toolchain, source_package


def release_fixture(folder, build=164):
    folder.mkdir(parents=True, exist_ok=True)
    prx = b'locally generated PRX'
    pbp = b'\0PBP' + struct.pack('<I8I', 0x10000, *([40] * 7 + [40 + len(prx)])) + prx
    for name, data in {'EBOOT.PBP': pbp, 'p4.prx': prx, 'p4.elf': b'ELF',
                       'p4data.bin': b'data', 'p4str.bin': b'strings'}.items():
        (folder / name).write_bytes(data)
    builder.save_json(folder / 'manifest.json', dict(build=build, release=True,
                     artifact_sha256={n: builder.digest(folder / n) for n in builder.ARTIFACTS}))


class SourceBuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.bundle = self.folder / 'package'; self.bundle.mkdir()
        source = self.bundle / 'source/port/main.c'
        source.parent.mkdir(parents=True); source.write_text('#define BUILD 164\n')
        self.archive = self.folder / 'upstream.zip'
        with zipfile.ZipFile(self.archive, 'w') as archive:
            archive.writestr(f'Persona4-Decompilation-{builder.UPSTREAM_COMMIT}/src/test.c', 'source')
        self.config = dict(version=2, mode='source-build', build=164, audio_streams=17375, movies=23,
                           source_sha256={'port/main.c': builder.digest(source)},
                           upstream=dict(commit=builder.UPSTREAM_COMMIT, url=builder.UPSTREAM_URL,
                                         tree_sha256=builder.tree_digest(self.archive)))
        builder.save_json(self.bundle / 'prepare.json', self.config)
        self.iso = self.folder / 'game.iso'; self.iso.write_bytes(b'original ELF fixture')
        self.cache = self.folder / 'cache with spaces'

    def invoke(self):
        return builder.build_eboot(self.iso, self.bundle, cache=self.cache, archive=self.archive)

    def fake_stage(self, workspace, name, args, env, log):
        if name == 'psp_census.py':
            census = workspace / 'build/census'; census.mkdir()
            (census / 'failed_compile.txt').write_text('\n')
        if name == 'psp_hw_debug.py':
            release_fixture(Path(args[-1]))

    def build_mocks(self):
        # Keep mock lifetimes explicit so failures cannot leak into later tests.
        from contextlib import ExitStack
        stack = ExitStack()
        stack.enter_context(patch.object(builder, 'RETAIL_SHA1', hashlib.sha1(self.iso.read_bytes()).hexdigest()))
        stack.enter_context(patch.object(builder, 'find_pspdev', return_value=self.folder))
        stack.enter_context(patch.object(builder, 'toolchain_identity', return_value={'sha256': 'toolchain', 'gcc': 'test'}))
        stack.enter_context(patch('extract_elf.extract', return_value=self.iso.read_bytes()))
        return stack

    def test_cache_rechecks_artifacts_and_rebuilds_corruption(self):
        with self.build_mocks(), patch.object(builder, 'run_stage', side_effect=self.fake_stage) as stage:
            release = self.invoke()
            self.assertEqual(stage.call_count, 4)
            self.assertEqual(self.invoke(), release)
            self.assertEqual(stage.call_count, 4)
            (release / 'p4data.bin').write_bytes(b'corruption')
            self.invoke()
            self.assertEqual(stage.call_count, 8)
            self.assertEqual((release / 'p4data.bin').read_bytes(), b'data')

    def test_changed_source_is_rejected_before_compiler(self):
        (self.bundle / 'source/port/main.c').write_text('changed')
        with patch.object(builder, 'find_pspdev') as compiler:
            with self.assertRaisesRegex(ValueError, 'incomplete or changed'):
                self.invoke()
            compiler.assert_not_called()

    def test_wrong_executable_is_rejected_before_compiler(self):
        with patch('extract_elf.extract', return_value=b'wrong'), patch.object(builder, 'find_pspdev') as compiler:
            with self.assertRaisesRegex(ValueError, 'Unsupported executable'):
                self.invoke()
            compiler.assert_not_called()

    def test_failed_compile_cannot_leave_a_reusable_result(self):
        with self.build_mocks(), patch.object(builder, 'run_stage', side_effect=RuntimeError('compile failed')):
            with self.assertRaisesRegex(RuntimeError, 'compile failed'):
                self.invoke()
        self.assertFalse(list(self.cache.rglob('ready.json')))
        self.assertFalse(list(self.cache.rglob('EBOOT.PBP')))

    def test_pbp_requires_the_matching_embedded_prx(self):
        release = self.folder / 'release'; release_fixture(release)
        pbp = release / 'EBOOT.PBP'; data = bytearray(pbp.read_bytes()); data[-1] ^= 1; pbp.write_bytes(data)
        manifest = json.loads((release / 'manifest.json').read_text())
        manifest['artifact_sha256']['EBOOT.PBP'] = builder.digest(pbp)
        builder.save_json(release / 'manifest.json', manifest)
        with self.assertRaisesRegex(ValueError, 'matching PRX'):
            builder.validate_result(release, 164)

    def test_archive_paths_cannot_escape_the_workspace(self):
        with zipfile.ZipFile(self.archive, 'w') as archive:
            archive.writestr(f'Persona4-Decompilation-{builder.UPSTREAM_COMMIT}/../../outside', 'bad')
        with self.assertRaisesRegex(ValueError, 'Unsafe package path'):
            builder.extract_upstream(self.archive, self.folder / 'workspace')
        self.assertFalse((self.folder / 'outside').exists())

    def test_build_only_installs_consistent_files_without_media_or_complete_marker(self):
        release = self.folder / 'release'; release_fixture(release)
        with patch('psp_prepare.inspect_disc', return_value=([], None, None)), \
             patch.object(builder, 'build_eboot', return_value=release), \
             patch('psp_audio.catalogue') as catalogue, patch('psp_video.movies') as movies:
            result = prepare(self.iso, self.folder / 'output', self.bundle, build_only=True)
            catalogue.assert_not_called(); movies.assert_not_called()
            self.assertEqual((result / 'EBOOT.PBP').read_bytes(), (release / 'EBOOT.PBP').read_bytes())
            self.assertTrue((result / 'build_manifest.json').exists())
            self.assertFalse((result / 'installation.json').exists())
            identity = json.loads((self.folder / 'output/.p4-work/state.json').read_text())
            self.assertEqual(identity['source_config_sha256'], builder.digest(self.bundle / 'prepare.json'))
            marker = result / 'installation.json'
            marker.write_text('existing complete installation marker')
            prepare(self.iso, self.folder / 'output', self.bundle, build_only=True)
            self.assertEqual(marker.read_text(), 'existing complete installation marker')

    def test_source_payload_contains_no_game_binaries_or_recipe(self):
        root = Path(__file__).resolve().parent.parent
        config = source_package(root, self.bundle)
        self.assertEqual(config['mode'], 'source-build')
        self.assertNotIn('words', config)
        self.assertNotIn('eboot_sha256', config)
        self.assertFalse((self.bundle / 'P4').exists())
        self.assertTrue(all(path.suffix in ('.c', '.h', '.S', '.py')
                            for path in (self.bundle / 'source').rglob('*') if path.is_file()))
        builder.verify_payload(self.bundle, config)

    def test_bundled_toolchain_keeps_provenance_without_local_install_paths(self):
        compiler = self.folder / 'maintainer tools'; compiler.mkdir()
        posix = compiler.as_posix()
        original = ('GCC 15.2.0; source snapshot abc123\n' + str(compiler) + '\n' +
                    posix + '\n/' + posix[0].lower() + posix[2:] + '/psp\n')
        (compiler / 'BUILDINFO.txt').write_text(original, encoding='utf-8')
        (compiler / 'compiler.exe').write_bytes(b'compiler')
        plugins = compiler / 'lib/bfd-plugins'; plugins.mkdir(parents=True)
        for name in ('libdep.a', 'libdep.la', 'valid.dll'):
            (plugins / name).write_bytes(b'plugin fixture')
        target = self.bundle / 'toolchain/pspdev'
        copy_toolchain(compiler, target)
        self.assertEqual((target / 'BUILDINFO.txt').read_text(encoding='utf-8'),
                         'GCC 15.2.0; source snapshot abc123\n${PSPDEV}\n${PSPDEV}\n${PSPDEV}/psp\n')
        self.assertEqual((compiler / 'BUILDINFO.txt').read_text(encoding='utf-8'), original)
        self.assertEqual((target / 'compiler.exe').read_bytes(), b'compiler')
        self.assertEqual([p.name for p in (target / 'lib/bfd-plugins').iterdir()], ['valid.dll'])
        self.assertTrue((plugins / 'libdep.a').is_file())

    def test_bundled_compiler_wins_over_environment_unless_explicitly_selected(self):
        bundled = self.bundle / 'toolchain/pspdev'
        other = self.folder / 'other SDK'
        for compiler in (bundled, other):
            (compiler / 'bin').mkdir(parents=True)
            for name in builder.TOOLS:
                (compiler / 'bin' / (name + '.exe')).touch()
            (compiler / 'psp/sdk/lib').mkdir(parents=True)
            (compiler / 'psp/sdk/lib/linkfile.prx').touch()
        with patch.dict(os.environ, {'PSPDEV': str(other)}):
            self.assertEqual(builder.find_pspdev(self.bundle), bundled.resolve())
            self.assertEqual(builder.find_pspdev(self.bundle, other), other.resolve())

    def test_compiler_download_is_verified_extracted_once_and_cleaned(self):
        import io
        payload = io.BytesIO()
        with zipfile.ZipFile(payload, 'w') as archive:
            archive.writestr('pspdev/bin/psp-gcc.exe', 'gcc')
        data = payload.getvalue()
        url = 'https://github.com/dreikk91/persona4-psp/releases/download/toolchain-gcc15.2/pspdev.zip'
        config = dict(reference=dict(recommended_toolchain=dict(url=url, archive_sha256='0' * 64)))
        root = builder.prepare_cache(self.cache)
        with patch('urllib.request.urlopen', return_value=io.BytesIO(data)):
            with self.assertRaises(RuntimeError):
                builder.download_toolchain(root, config)
        self.assertFalse((root / 'toolchain').exists())
        config['reference']['recommended_toolchain']['archive_sha256'] = hashlib.sha256(data).hexdigest()
        with patch('urllib.request.urlopen', return_value=io.BytesIO(data)) as fetch:
            compiler = builder.download_toolchain(root, config)
            self.assertEqual(builder.download_toolchain(root, config), compiler)
        fetch.assert_called_once()
        self.assertEqual((compiler / 'bin/psp-gcc.exe').read_text(), 'gcc')
        builder.clean_cache(self.cache, include_downloads=True)
        self.assertFalse((root / 'toolchain').exists())


class WindowedWorkers(unittest.TestCase):
    def test_source_cli_uses_the_worker_bootstrap_too(self):
        with patch.object(builder.sys, 'frozen', False, create=True):
            command = builder.script_command(Path('tools/psp_census.py'), '--compile')
        self.assertIn('--build-worker', command)
        self.assertTrue(command[2].endswith('psp_prepare.py'))
        self.assertEqual(command[-2:], [str(Path('tools/psp_census.py')), '--compile'])

    @unittest.skipUnless(os.name == 'nt', 'Windows process flags')
    def test_hidden_workers_preserve_other_creation_flags(self):
        sentinel = object()
        with patch.object(subprocess.Popen, '__init__') as original:
            _hide_child_windows()
            subprocess.Popen.__init__(sentinel, ['compiler'], creationflags=subprocess.CREATE_NEW_PROCESS_GROUP)
            original.assert_called_once_with(sentinel, ['compiler'], creationflags=(
                subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP))


if __name__ == '__main__':
    unittest.main()
