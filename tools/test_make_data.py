"""Retail markers must name definitions, never forward declarations."""
import unittest

from psp_make_data import MARKED, ROOT


class FunctionMarkers(unittest.TestCase):
    def test_definition_with_callback_parameter(self):
        source = '// FUN_00123450\n/* callback */\nvoid actual(int (*cb)(int)) { cb(1); }'
        self.assertEqual(MARKED.findall(source), [('00123450', 'actual')])

    def test_declaration_cannot_alias_another_function(self):
        source = '// FUN_00202B60\ns32 ready(void *p);\nu8 *create(s32 task) { return 0; }'
        self.assertEqual(MARKED.findall(source), [])

    def test_battle_panel_constructor_is_not_readiness_callback(self):
        source = (ROOT / 'ext/Persona4-Decompilation/src/Battle/btlPanel.c').read_text()
        marked = dict(MARKED.findall(source))
        self.assertEqual(marked['00202890'], 'btlPanelIsEnabled')
        self.assertNotIn('00202B60', marked)


if __name__ == '__main__':
    unittest.main()
