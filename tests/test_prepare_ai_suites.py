import importlib.util
import pathlib
import unittest

script = pathlib.Path(__file__).parents[1] / "tools" / "prepare_ai_suites.py"
spec = importlib.util.spec_from_file_location("prepare_ai_suites", script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PrepareSuitesTests(unittest.TestCase):
    def test_routes_both_operations(self):
        source = "a = sSPBasic->AcquireSuite(name, v, out);\nb = sSPBasic->ReleaseSuite(name, v);"
        result = module.prepare(source)
        self.assertIn("SlippyAcquireSuite(name, v, out)", result)
        self.assertIn("SlippyReleaseSuite(name, v)", result)
        self.assertNotIn("sSPBasic->", result)

    def test_changed_sdk_loader_requires_review(self):
        for source in ("", "sSPBasic->AcquireSuite();" * 2 + "sSPBasic->ReleaseSuite();"):
            with self.assertRaises(ValueError):
                module.prepare(source)


if __name__ == "__main__":
    unittest.main()
