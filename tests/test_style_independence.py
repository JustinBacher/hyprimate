import re
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INCLUDE = re.compile(r'^\s*#\s*include\s*(["<])([^">]+)[">]', re.MULTILINE)
SOURCE_EXTENSIONS = {'.h', '.hh', '.hpp', '.hxx', '.c', '.cc', '.cpp', '.cxx'}


def include_violations(source_root, category='animations'):
    animations = source_root / category
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

def configuration_violations(source_root, category='animations'):
    animations = source_root / category
    styles = {path.name for path in animations.iterdir()
              if path.is_dir() and (path / 'Style.cmake').is_file()}
    forbidden = re.compile(rb'SConfig|m_config')
    violations = []
    for owner in sorted(styles | {'shared'}):
        for source in sorted((animations / owner).rglob('*')):
            if not source.is_file():
                continue
            content = source.read_bytes()
            for match in forbidden.finditer(content):
                line = content.count(b'\n', 0, match.start()) + 1
                violations.append(f'{source.relative_to(source_root)}:{line}: '
                                  f'{match.group().decode()}')
    return violations

def runtime_style_violations(source_root):
    animations = source_root / 'animations'
    styles = sorted(path.name for path in animations.iterdir()
                    if path.is_dir() and (path / 'Style.cmake').is_file())
    names = '|'.join(re.escape(name.capitalize()) for name in styles)
    directories = '|'.join(re.escape(name) for name in styles)
    patterns = [
        rf'animations/(?:{directories})/',
        rf'\bS(?:{names})(?:Style|Module)\b',
        rf'\bC(?:{names})\w*\b',
        rf'\bStyle\s*::\s*(?:{names})\b',
    ]
    # Workspace styles are reached only through the workspace style list as well.
    workspace = source_root / 'workspace'
    workspace_styles = sorted(path.name for path in workspace.iterdir()
                              if path.is_dir() and (path / 'Style.cmake').is_file()) if workspace.is_dir() else []
    if workspace_styles:
        workspace_names = '|'.join(re.escape(name) for name in workspace_styles)
        patterns.append(rf'(?i:workspace/(?:{workspace_names})/|\b\w*(?:{workspace_names})\w*\b)')
    forbidden = re.compile('|'.join(f'(?:{pattern})' for pattern in patterns))
    violations = []
    for source in sorted(source_root.glob('PluginRuntime.*')):
        if source.suffix not in SOURCE_EXTENSIONS:
            continue
        generated_macro = False
        for line_number, line in enumerate(source.read_text(encoding='utf-8').splitlines(), 1):
            if re.match(r'\s*#\s*define\s+HYPRIMATE_\w+', line):
                generated_macro = True
            if generated_macro:
                generated_macro = line.rstrip().endswith('\\')
                continue
            for match in forbidden.finditer(line):
                violations.append(f'{source.relative_to(source_root)}:{line_number}: {match.group()}')
    return violations

def workspace_orchestration_style_violations(source_root):
    workspace = source_root / 'workspace'
    styles = sorted(path.name for path in workspace.iterdir()
                    if path.is_dir() and (path / 'Style.cmake').is_file())
    if not styles:
        return []
    names = '|'.join(re.escape(name) for name in styles)
    # The list is the one composition root where concrete styles must be named.
    forbidden = re.compile(rf'(?i)(?:workspace/(?:{names})/[\w./]*|\b\w*(?:{names})\w*\b)')
    violations = []
    for source in sorted(workspace.iterdir()):
        if not source.is_file() or source.suffix not in SOURCE_EXTENSIONS or source.name == 'WorkspaceStyleList.hpp':
            continue
        for line_number, line in enumerate(source.read_text(encoding='utf-8').splitlines(), 1):
            for match in forbidden.finditer(line):
                violations.append(f'{source.relative_to(source_root)}:{line_number}: {match.group()}')
    return violations





class StyleIndependenceTests(unittest.TestCase):
    def test_repository_style_dependencies(self):
        violations = include_violations(ROOT / 'src')
        self.assertFalse(violations, '\n' + '\n'.join(violations))

    def test_workspace_capture_scope_is_style_neutral(self):
        source = (ROOT / 'src/workspace/WorkspaceCaptureScope.hpp').read_text(encoding='utf-8')
        self.assertNotRegex(source, r'animations/(?:bubble|swarm)/|\b[CS]Bubble\w*|\b[CS]Swarm\w*')

    def test_legacy_state_and_runtime_friendship_are_absent(self):
        source_root = ROOT / 'src'
        paths = list(source_root.glob('PluginRuntime.*')) + [
            source_root / 'AnimationTransformer.hpp', source_root / 'StyleTypes.hpp']
        forbidden = re.compile(
            r'\b(?:SStyleState|SCloseStyleData|built_for|builtFor|cancelOpening|cancelMovement)\b'
            r'|friend\s+struct')
        for path in paths:
            with self.subTest(path=path.name):
                self.assertNotRegex(path.read_text(encoding='utf-8'), forbidden)

    def test_repository_style_configuration_is_independent(self):
        violations = configuration_violations(ROOT / 'src')
        self.assertFalse(violations, '\n' + '\n'.join(violations))

    def test_workspace_style_dependencies_and_configuration(self):
        source_root = ROOT / 'src'
        violations = (include_violations(source_root, 'workspace') +
                      configuration_violations(source_root, 'workspace'))
        self.assertFalse(violations, '\n' + '\n'.join(violations))

    def test_workspace_orchestration_does_not_name_styles(self):
        violations = workspace_orchestration_style_violations(ROOT / 'src')
        self.assertFalse(violations, '\n' + '\n'.join(violations))

    def test_workspace_orchestration_rule_detects_names_and_allows_list(self):
        with tempfile.TemporaryDirectory() as tmp:
            source_root = Path(tmp) / 'src'
            workspace = source_root / 'workspace'
            (workspace / 'rotation').mkdir(parents=True)
            (workspace / 'rotation' / 'Style.cmake').touch()
            (workspace / 'WorkspaceStyleList.hpp').write_text('using Styles = RotationStyle;\n')
            (workspace / 'WorkspaceTurn.cpp').write_text(
                '#include "workspace/rotation/RotationStyle.hpp"\n'
                'auto style = RotationStyle{};\n')
            violations = workspace_orchestration_style_violations(source_root)
            self.assertEqual(len(violations), 2, violations)

    def test_workspace_independence_rules_detect_violations(self):
        with tempfile.TemporaryDirectory() as tmp:
            source_root = Path(tmp) / 'src'
            workspace = source_root / 'workspace'
            for name in ('rotation', 'other'):
                (workspace / name).mkdir(parents=True)
                (workspace / name / 'Style.cmake').touch()
            (workspace / 'rotation' / 'Style.hpp').touch()
            (workspace / 'other' / 'Style.hpp').write_text(
                '#include "../rotation/Style.hpp"\nSConfig config;\n')
            self.assertEqual(len(include_violations(source_root, 'workspace')), 1)
            self.assertEqual(len(configuration_violations(source_root, 'workspace')), 1)

    def test_runtime_does_not_name_styles(self):
        violations = runtime_style_violations(ROOT / 'src')
        self.assertFalse(violations, '\n' + '\n'.join(violations))

    def test_configuration_check_covers_styles_and_shared(self):
        with tempfile.TemporaryDirectory() as tmp:
            source_root = Path(tmp) / 'src'
            animations = source_root / 'animations'
            for name in ('liquid', 'shared'):
                (animations / name).mkdir(parents=True)
            (animations / 'liquid' / 'Style.cmake').touch()
            (animations / 'liquid' / 'Liquid.hpp').write_text('SConfigValues config;\n')
            (animations / 'shared' / 'Common.hpp').write_text('auto value = m_config;\n')
            violations = configuration_violations(source_root)
            self.assertEqual(len(violations), 2, violations)
            self.assertTrue(any('SConfig' in violation for violation in violations))
            self.assertTrue(any('m_config' in violation for violation in violations))

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
