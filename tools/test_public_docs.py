"""Keep published documentation independent of the maintainer's filesystem."""
from pathlib import Path
import re
import unittest


class PublicDocumentationTests(unittest.TestCase):
    def test_no_absolute_host_paths(self):
        root = Path(__file__).resolve().parent.parent
        paths = sorted(root.glob('*.md')) + sorted((root / 'docs').rglob('*.md'))
        host_path = re.compile(r'(?i)(?<![a-z0-9])[a-z]:[\\/]|/Users/|/home/|~[/\\]|\\\\[^\s\\]+\\')
        for path in paths:
            with self.subTest(path=path.relative_to(root).as_posix()):
                self.assertIsNone(host_path.search(path.read_text(encoding='utf-8')),
                                  'Use a relative path or a named environment variable.')


if __name__ == '__main__':
    unittest.main()
