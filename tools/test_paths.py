"""Host checks for PSP launch-directory resolution."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

class PathsTest(unittest.TestCase):
    def test_launch_locations(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as temp:
            lib = Path(temp) / "paths.so"
            subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                            str(root / "port/paths.c"), "-o", str(lib)], check=True)
            api = ctypes.CDLL(str(lib))
            api.p4_paths_init.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
            cases = [
                (b"ms0:/PSP/GAME/P4/EBOOT.PBP", None, b"ms0:/PSP/GAME/P4/"),
                (b"ms0:/PSP/GAME/CAT_Ports/Persona4 PORTABLE/EBOOT.PBP",
                 b"ms0:/", b"ms0:/PSP/GAME/CAT_Ports/Persona4 PORTABLE/"),
                (b"./EBOOT.PBP", b"ms0:/PSP/GAME/CAT_Ports/P4/",
                 b"ms0:/PSP/GAME/CAT_Ports/P4/"),
                (None, b"ms0:/PSP/GAME/P4", b"ms0:/PSP/GAME/P4/"),
            ]
            for exe, cwd, expected in cases:
                self.assertEqual(api.p4_paths_init(exe, cwd), 0)
                self.assertEqual((ctypes.c_char * 160).in_dll(api, "p4_game_dir").value, expected)
                self.assertEqual((ctypes.c_char * 176).in_dll(api, "p4_save_dir").value, expected+b"saves")
                self.assertEqual((ctypes.c_char * 176).in_dll(api, "p4_sound_dir").value, expected+b"SOUND/")
            for exe, cwd in [(None, None), (b"ef0:/P4/EBOOT.PBP", b"ms0:/"),
                             (b"ms0:/P4/../EBOOT.PBP", None),
                             (b"ms0:/"+b"x"*160+b"/EBOOT.PBP", None),
                             (b"ms0:/P4/", None), (b"/EBOOT.PBP", b"ms0:/")]:
                self.assertEqual(api.p4_paths_init(exe, cwd), -1)

if __name__ == "__main__":
    unittest.main()
