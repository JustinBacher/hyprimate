import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / 'tools' / 'generate_config.py'


class ConfigGeneratorTests(unittest.TestCase):
    def test_schema_change_updates_documentation_and_registration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            schema = root / 'settings.json'
            schema.write_text(json.dumps([{'name': 'strength', 'member': 'm_strength', 'type': 'float',
                'default': 0.7, 'min': 0, 'max': 1.25, 'description': 'Example strength'}]))
            readme = root / 'README.md'
            readme.write_text('Before\n<!-- BEGIN GENERATED CONFIGURATION -->\nold\n<!-- END GENERATED CONFIGURATION -->\nAfter\n')
            header = root / 'config.inc'
            result = subprocess.run([sys.executable, str(GENERATOR), '--schema', str(schema), '--readme', str(readme), '--header', str(header)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('0.7', readme.read_text())
            self.assertIn('1.25', readme.read_text())
            self.assertIn('0.7F', header.read_text())
            self.assertIn('1.25F', header.read_text())
            self.assertTrue(readme.read_text().startswith('Before\n'))
            self.assertTrue(readme.read_text().endswith('\nAfter\n'))

    def test_check_detects_stale_readme_without_modifying_it(self):
        with tempfile.TemporaryDirectory() as directory:
            readme = Path(directory) / 'README.md'
            original = '<!-- BEGIN GENERATED CONFIGURATION -->\nstale\n<!-- END GENERATED CONFIGURATION -->\n'
            readme.write_text(original)
            result = subprocess.run([sys.executable, str(GENERATOR), '--readme', str(readme), '--check'], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('stale', result.stderr)
            self.assertEqual(readme.read_text(), original)

    def test_invalid_markers_leave_readme_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            readme = Path(directory) / 'README.md'
            for original in ('no markers', '<!-- END GENERATED CONFIGURATION -->\n<!-- BEGIN GENERATED CONFIGURATION -->',
                             '<!-- BEGIN GENERATED CONFIGURATION -->\n<!-- BEGIN GENERATED CONFIGURATION -->\n<!-- END GENERATED CONFIGURATION -->'):
                with self.subTest(original=original):
                    readme.write_text(original)
                    result = subprocess.run([sys.executable, str(GENERATOR), '--readme', str(readme)], capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(readme.read_text(), original)


if __name__ == '__main__':
    unittest.main()
