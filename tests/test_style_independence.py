import re
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INCLUDE = re.compile(r'^\s*#\s*include\s*(["<])([^">]+)[">]', re.MULTILINE)
SOURCE_EXTENSIONS = {'.h', '.hh', '.hpp', '.hxx', '.c', '.cc', '.cpp', '.cxx'}


def include_violations(source_root):
    animations = source_root / 'animations'
    styles = {path.name for path in animations.iterdir()
              if path.is_dir() and (path / 'Style.cmake').is_file()}
    violations = []
    for owner in sorted(styles | {'shared'}):
        directory = animations / owner
        for source in sorted(directory.rglob('*')):
            if not source.is_file() or source.suffix not in SOURCE_EXTENSIONS:
                continue
            text = source.read_text(encoding='utf-8')
            for match in INCLUDE.finditer(text):
                delimiter, name = match.groups()
                candidates = ([source.parent / name] if delimiter == '"' else []) + [source_root / name]
                # Match the compiler's quoted-include search order when a file exists.
                resolved = next((path.resolve() for path in candidates if path.is_file()), None)
                if resolved is None:
                    resolved = candidates[-1].resolve()
                try:
                    parts = resolved.relative_to(animations.resolve()).parts
                except ValueError:
                    continue
                if len(parts) >= 2 and parts[0] in styles and parts[0] != owner:
                    line = text.count('\n', 0, match.start()) + 1
                    violations.append(f'{source.relative_to(source_root)}:{line}: {name} ({owner} -> {parts[0]})')
    return violations


class StyleIndependenceTests(unittest.TestCase):
    def test_repository_style_dependencies(self):
        violations = include_violations(ROOT / 'src')
        self.assertFalse(violations, '\n' + '\n'.join(violations))

    def test_resolves_relative_and_source_root_includes(self):
        with tempfile.TemporaryDirectory() as tmp:
            source_root = Path(tmp) / 'src'
            animations = source_root / 'animations'
            for name in ('liquid', 'paper', 'shared'):
                (animations / name).mkdir(parents=True)
                if name != 'shared':
                    (animations / name / 'Style.cmake').touch()
            (animations / 'liquid' / 'Liquid.hpp').touch()
            (animations / 'paper' / 'Paper.hpp').write_text(
                '#include "../liquid/Liquid.hpp"\n'
                '#include "animations/liquid/Liquid.hpp"\n'
                '#include "../shared/Common.hpp"\n')
            (animations / 'shared' / 'Common.hpp').write_text(
                '#include "animations/liquid/Liquid.hpp"\n')
            violations = include_violations(source_root)
            self.assertEqual(len(violations), 3, violations)
            self.assertTrue(any('paper -> liquid' in violation for violation in violations))
            self.assertTrue(any('shared -> liquid' in violation for violation in violations))

    def test_same_style_and_shared_includes_are_allowed(self):
        with tempfile.TemporaryDirectory() as tmp:
            source_root = Path(tmp) / 'src'
            liquid = source_root / 'animations' / 'liquid'
            shared = source_root / 'animations' / 'shared'
            liquid.mkdir(parents=True)
            shared.mkdir()
            (liquid / 'Style.cmake').touch()
            (liquid / 'Own.hpp').write_text('#include "Own.hpp"\n#include "../shared/Common.hpp"\n')
            (shared / 'Common.hpp').write_text('#include "Common.hpp"\n')
            self.assertEqual(include_violations(source_root), [])


if __name__ == '__main__':
    unittest.main()
