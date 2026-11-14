"""Check that the generator audit does not mistake a partial graph for support."""
import unittest

from psp_generator_audit import boot_frontier, runtime_bindings


class RuntimeBindings(unittest.TestCase):
    def test_only_explicit_code_exports_count(self):
        symbols = ('00000000 T Replace\n00000004 D Data\n'
                   '00000008 t private\n0000000c T __wrap_Original\n'
                   '00000010 T p4r_001002c0\n')
        self.assertEqual(runtime_bindings(symbols, {'Replace': 0x100100, 'Data': 0x100200,
                                                   'private': 0x100300, '__wrap_Original': 0x100400}),
                         {0x100100: 'Replace', 0x1002c0: 'p4r_001002c0'})


class BootFrontier(unittest.TestCase):
    def test_refused_descendant_and_unknown_target_are_reported(self):
        graph = {1: {2}, 2: {3, 4}, 3: {5}}
        statuses = {1: 'translation candidate', 2: 'translation candidate', 3: 'VU0'}
        blocked, visited = boot_frontier(1, graph, statuses)
        self.assertEqual(visited, 4)
        self.assertEqual(blocked, [
            {'address': '00000003', 'status': 'VU0', 'path': ['00000001', '00000002', '00000003']},
            {'address': '00000004', 'status': 'unknown direct target',
             'path': ['00000001', '00000002', '00000004']}])

    def test_runtime_boundary_is_not_traversed_and_cycles_terminate(self):
        blocked, visited = boot_frontier(1, {1: {1, 2}, 2: {3}},
                                         {1: 'translation candidate', 2: 'runtime candidate'})
        self.assertEqual((blocked, visited), ([], 2))

    def test_missing_boot_is_not_invented(self):
        self.assertEqual(boot_frontier(None, {}, {}), ([], 0))


if __name__ == '__main__':
    unittest.main()
