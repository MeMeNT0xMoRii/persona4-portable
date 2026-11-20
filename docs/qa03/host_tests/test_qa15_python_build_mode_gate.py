import importlib.util
import os
import subprocess
import sys
import unittest
from pathlib import Path

SCRIPT = Path(__file__).with_name('qa15_python_build_mode_gate.py')
spec = importlib.util.spec_from_file_location('qa15_build_mode', SCRIPT)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class PythonBuildModeGateTests(unittest.TestCase):
    def test_01_normal_flags(self):
        self.assertEqual(gate.inspect(0, None), [])

    def test_02_optimized(self):
        self.assertIn('PYTHON_RUNTIME_OPTIMIZED:1', gate.inspect(1, None))

    def test_03_double_optimized(self):
        self.assertIn('PYTHON_RUNTIME_OPTIMIZED:2', gate.inspect(2, None))

    def test_04_environment(self):
        self.assertTrue(gate.inspect(0, '1'))

    def test_05_runtime_normal(self):
        env = dict(os.environ)
        env.pop('PYTHONOPTIMIZE', None)
        x = subprocess.run([sys.executable, str(SCRIPT)], capture_output=True, text=True, env=env)
        self.assertEqual(x.returncode, 0, x.stdout + x.stderr)

    def test_06_runtime_optimized(self):
        env = dict(os.environ)
        env.pop('PYTHONOPTIMIZE', None)
        x = subprocess.run([sys.executable, '-O', str(SCRIPT)], capture_output=True, text=True, env=env)
        self.assertEqual(x.returncode, 2, x.stdout + x.stderr)

    def test_07_runtime_environment(self):
        env = dict(os.environ, PYTHONOPTIMIZE='1')
        x = subprocess.run([sys.executable, str(SCRIPT)], capture_output=True, text=True, env=env)
        self.assertEqual(x.returncode, 2, x.stdout + x.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
