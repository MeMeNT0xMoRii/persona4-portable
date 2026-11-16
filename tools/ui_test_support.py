"""Host fixtures for native UI comparisons; 32-bit layouts use the existing MSVC installation."""
from pathlib import Path
import subprocess, tempfile, unittest, shutil, re
ROOT=Path(__file__).resolve().parents[1]

def function(source, name):
    match = re.search(r'\b' + re.escape(name) + r'[^;{}]*\)\s*(?:/\*[^*]*\*/\s*)?\{', source)
    assert match, name
    start = source.rfind('\n', 0, match.start()) + 1
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}'); end += 1
    return source[start:end]

class UiTestCase(unittest.TestCase):
    def check_32(self, code):
        vswhere = Path('C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe')
        if not vswhere.exists(): self.skipTest('32-bit Visual C toolchain unavailable')
        install = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
                                           'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                                           '-property', 'installationPath'], text=True).strip()
        if not install: self.skipTest('32-bit Visual C toolchain unavailable')
        with tempfile.TemporaryDirectory(prefix='p4-body-test-') as directory:
            folder = Path(directory); (folder / 'check.c').write_text(code)
            batch = folder / 'build.cmd'
            batch.write_text(f'@echo off\ncall "{install}/VC/Auxiliary/Build/vcvars32.bat" >nul\n'
                             f'cl /nologo /std:c11 /O2 /fp:precise /I"{ROOT / "port"}" check.c /Fe:check.exe\n')
            result = subprocess.run(['cmd', '/c', str(batch)], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(folder / 'check.exe')], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    def check_c(self, code):
        cc = shutil.which('gcc')
        if not cc:
            self.skipTest('host gcc unavailable')
        with tempfile.TemporaryDirectory(prefix='p4-particle-test-') as directory:
            source = Path(directory) / 'check.c'
            exe = Path(directory) / 'check.exe'
            source.write_text(code)
            command = [cc, '-std=gnu11', '-O2', '-fno-builtin',
                       f'-I{ROOT / "port"}', f'-I{ROOT / "ext/Persona4-Decompilation/include"}',
                       str(source), '-o', str(exe), '-lm']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(exe)], timeout=10)
            self.assertEqual(result.returncode, 0)
