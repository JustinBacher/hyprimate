import importlib.util
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / 'tools' / 'generate_config.py'
SPEC = importlib.util.spec_from_file_location('generate_config', GENERATOR)
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)

SCHEMA_PATH = ROOT / 'config' / 'settings.json'
README = ROOT / 'README.md'
LUA = ROOT / 'tests' / 'nested' / 'hyprland.lua'
SCHEMA = GEN.expanded_settings(json.loads(SCHEMA_PATH.read_text()))
REGISTERED = SCHEMA
LUA_BEGIN = '-- BEGIN GENERATED PLUGIN CONFIG'
LUA_END = '-- END GENERATED PLUGIN CONFIG'


def lua_leaf_paths(text):
    """Return dotted paths of every leaf (including empty tables) in the hyprimate Lua table."""
    stack, leaves, in_plugin = [], set(), False
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith('["hyprimate"] = {'):
            in_plugin = True
            continue
        if not in_plugin:
            continue
        if stripped in ('},', '}'):
            if stack:
                stack.pop()
            continue
        table = re.match(r'([a-z_]+) = \{$', stripped)
        if table:
            stack.append(table.group(1))
            continue
        leaf = re.match(r'([a-z_]+) = (.*),$', stripped)
        if leaf:
            leaves.add('.'.join(stack + [leaf.group(1)]))
    return leaves


def nested_region(text):
    start = text.index(LUA_BEGIN) + len(LUA_BEGIN)
    end = text.index(LUA_END)
    return '\n'.join(line[3:] if line.startswith('-- ') else line.lstrip('-').lstrip()
                     for line in text[start:end].strip('\n').splitlines())


class ConfigGeneratorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def run_generator(self, *args):
        return subprocess.run([sys.executable, str(GENERATOR), *args], cwd=ROOT,
                              capture_output=True, text=True)

    def copies(self):
        readme = self.tmp / 'README.md'
        lua = self.tmp / 'hyprland.lua'
        shutil.copy(README, readme)
        shutil.copy(LUA, lua)
        return readme, lua

    def write_schema(self, items):
        path = self.tmp / 'settings.json'
        path.write_text(json.dumps(items, indent=2))
        return path

    def test_every_path_is_under_window_root(self):
        for item in SCHEMA:
            self.assertEqual(item['path'].split('.')[0], 'window', item['path'])

    def test_close_options_are_side_and_splash(self):
        paths = {item['path'] for item in SCHEMA}
        self.assertIn('window.close.options.liquid.side', paths)
        self.assertIn('window.close.options.liquid.splash', paths)
        self.assertFalse(any(p.endswith(('close_side', 'close_splash')) for p in paths))

    def test_every_animation_style_setting_has_a_shared_key(self):
        paths = {item['path'] for item in SCHEMA}
        for path in paths:
            parts = path.split('.')
            if len(parts) == 5 and parts[1] in ('open', 'close', 'movement') and parts[2] == 'options':
                with self.subTest(path=path):
                    self.assertIn('.'.join(('window', 'options', *parts[3:])), paths)

    def test_every_global_swarm_setting_has_animation_overrides(self):
        paths = {item['path'] for item in SCHEMA}
        for item in json.loads(SCHEMA_PATH.read_text()):
            path = item['path']
            if path.startswith('window.options.swarm.'):
                suffix = path.removeprefix('window.options.')
                for direction in ('open', 'close', 'movement'):
                    with self.subTest(path=path, direction=direction):
                        self.assertIn(f'window.{direction}.options.{suffix}', paths)

    def test_registration_keys_and_members(self):
        header = GEN.config_header(SCHEMA)
        for item in REGISTERED:
            self.assertIn(f'"{GEN.key(item)}"', header)
        self.assertEqual(len(re.findall(r'= makeShared<', header)), len(REGISTERED))
        self.assertIn('plugin:hyprimate:window:close:options:liquid:splash', header)
        self.assertIn('m_closeSplash', header)

    def test_string_choices_and_float_ranges_are_enforced(self):
        header = GEN.config_header(SCHEMA)
        for item in REGISTERED:
            if item['type'] == 'string' and 'choices' in item:
                self.assertIn(GEN.key(item) + ' must be one of: ' + ', '.join(item['choices']), header)
            if item['type'] == 'float':
                self.assertIn(f'.min = {GEN.float_literal(item["min"])}', header)

    def test_lua_leaves_match_schema_paths(self):
        self.assertEqual(lua_leaf_paths(GEN.lua_config(SCHEMA)),
                         {item['path'] for item in SCHEMA if not item.get('optional')})

    def test_nested_example_leaves_match_schema_paths(self):
        region = nested_region(LUA.read_text())
        self.assertEqual(lua_leaf_paths(region),
                         {item['path'] for item in SCHEMA if not item.get('optional')})

    def test_readme_table_documents_only_shared_and_control_settings(self):
        block = GEN.documentation(SCHEMA)
        rows = re.findall(r'^\| `([^`]+)` \| ([^\n]+)$', block, re.MULTILINE)
        documented = {path for path, _ in rows}
        self.assertEqual(documented, {item['path'].replace('.', ':') for item in SCHEMA
                                      if not item.get('inherited')})
        self.assertEqual(len(rows), len(documented))
        self.assertIn('| Setting | Type / default | Numeric range | Meaning |', block)
        for item in SCHEMA:
            if item.get('inherited'):
                continue
            path = item['path'].replace('.', ':')
            row = next(text for key, text in rows if key == path)
            expected_range = (f'{item["min"]}–{item["max"]}'
                              if item['type'] in ('int', 'float') else '—')
            self.assertIn(f' | {expected_range} | ', row)

    def test_readme_example_includes_all_shared_options_without_changing_defaults(self):
        block = GEN.documentation(SCHEMA)
        example = block.split('```lua\n', 1)[1].split('\n```', 1)[0]
        uncommented = re.sub(r'^(\s*)-- (?=[a-z_]+ = )', r'\1', example, flags=re.MULTILINE)
        self.assertEqual(lua_leaf_paths(uncommented),
                         {item['path'] for item in SCHEMA if not item.get('inherited')})
        active = lua_leaf_paths(example)
        for item in SCHEMA:
            if not item['path'].startswith('window.options.') or not item.get('optional'):
                continue
            suffix = item['path'].removeprefix('window.options.')
            values = {other['default'] for other in SCHEMA if other.get('inherited')
                      and other['path'].split('.options.', 1)[1] == suffix}
            with self.subTest(path=item['path']):
                self.assertEqual(item['path'] in active, len(values) <= 1)

    def test_readme_describes_differing_animation_defaults_and_meanings(self):
        block = GEN.documentation(SCHEMA)
        for style, opening, closing in [('paper', '0.6', '0.5'),
                                        ('cloth', '1.0', '0.9'),
                                        ('bubble', '0.62', '0.42')]:
            row = next(line for line in block.splitlines()
                       if line.startswith(f'| `window:options:{style}:duration` |'))
            self.assertIn(f'open `{opening}`', row)
            self.assertIn(f'close `{closing}`', row)
            self.assertIn('Open:', row)
            self.assertIn('Close:', row)
            self.assertNotIn(f'Shared {style} duration setting.', row)
        self.assertIn('window.<animation>.options.<style>', block)
        self.assertIn('clamped to the receiving animation', block)

    def test_documentation_does_not_change_nested_lua_example(self):
        before = GEN.lua_config(SCHEMA)
        GEN.documentation(SCHEMA)
        self.assertEqual(GEN.lua_config(SCHEMA), before)
        self.assertEqual(before, nested_region(LUA.read_text()))

    def test_strength_zero_contract_is_documented_in_schema(self):
        strengths = [item for item in json.loads(SCHEMA_PATH.read_text()) if item['path'].endswith('.strength')]
        self.assertTrue(strengths)
        for strength in strengths:
            self.assertEqual(strength['min'], 0)
            self.assertIn('zero', strength['description'].lower())

    def test_repository_artifacts_are_current(self):
        result = self.run_generator('--readme', str(README), '--lua', str(LUA), '--check')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_schema_change_regenerates_outputs_and_flags_stale_repo_copies(self):
        items = json.loads(SCHEMA_PATH.read_text())
        next(item for item in items if item['path'] == 'window.movement.options.liquid.strength')['default'] = 0.7
        next(item for item in items if item['path'] == 'window.options.swarm.count')['default'] = 300
        schema = self.write_schema(items)
        readme, lua = self.copies()
        header = self.tmp / 'config_generated.hpp'
        result = self.run_generator('--schema', str(schema), '--readme', str(readme), '--lua', str(lua),
                                    '--header', str(header))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('count = 300,', lua.read_text())
        self.assertNotIn('strength = 0.7,', lua.read_text())
        self.assertIn('0.7F', header.read_text())
        self.assertIn('count = 300,', readme.read_text())
        self.assertIn('movement `0.7`', readme.read_text())
        stale = self.run_generator('--schema', str(schema), '--readme', str(README), '--lua', str(LUA), '--check')
        self.assertEqual(stale.returncode, 1)

    def test_invalid_schema_writes_nothing(self):
        items = json.loads(SCHEMA_PATH.read_text())
        items.append(dict(items[0]))
        schema = self.write_schema(items)
        readme, lua = self.copies()
        before = readme.read_text(), lua.read_text()
        result = self.run_generator('--schema', str(schema), '--readme', str(readme), '--lua', str(lua))
        self.assertEqual(result.returncode, 1)
        self.assertIn('duplicate setting path', result.stderr)
        self.assertEqual((readme.read_text(), lua.read_text()), before)

    def test_table_type_is_rejected(self):
        items = json.loads(SCHEMA_PATH.read_text())
        items.append({'path': 'window.example', 'type': 'table', 'description': 'x', 'member': 'm_example'})
        with self.assertRaisesRegex(ValueError, 'unsupported type'):
            GEN.validate(items)

    def test_string_default_must_be_a_choice(self):
        items = json.loads(SCHEMA_PATH.read_text())
        style = next(item for item in items if item['path'] == 'window.style')
        style['default'] = 'wiggle'
        with self.assertRaisesRegex(ValueError, 'string default'):
            GEN.validate(items)

    def test_int_defaults_must_be_in_integer_range(self):
        item = {'path': 'window.example', 'member': 'm_example', 'type': 'int',
                'default': 2, 'min': 1, 'max': 3, 'description': 'Example.'}
        for field, value in [('default', 0), ('default', 2.0), ('default', True),
                             ('min', False), ('max', 3.0)]:
            with self.subTest(field=field, value=value):
                invalid = dict(item, **{field: value})
                with self.assertRaisesRegex(ValueError, 'window.example: int default'):
                    GEN.validate([invalid])

    def test_set_default_rejects_unknown_or_empty_tokens(self):
        item = {'path': 'window.example', 'member': 'm_example', 'type': 'set',
                'choices': ['top', 'bottom'], 'all': 'all', 'default': 'top', 'description': 'Example.'}
        for default in ('top,unknown', '', 'top,', 'all,unknown'):
            with self.subTest(default=default):
                with self.assertRaisesRegex(ValueError, 'window.example: set default'):
                    GEN.validate([dict(item, default=default)])

    def test_set_enum_collisions_are_rejected(self):
        item = {'path': 'window.open.sides', 'member': 'm_openSides', 'type': 'set',
                'choices': ['top', 'bottom'], 'all': 'all', 'default': 'all', 'description': 'Example.'}
        for other in (dict(item, path='window.close.sides', member='m_closeSides', choices=['top']),
                      dict(item, path='window.close.sides', member='m_closeSides', all='every', default='top'),
                      dict(item, path='window.close.sides', member='m_closeSides', type='string', default='top')):
            with self.subTest(other=other):
                with self.assertRaises(ValueError):
                    GEN.validate([item, other])

    def test_set_defaults_and_choices(self):
        item = {'path': 'window.example', 'member': 'm_example', 'type': 'set',
                'choices': ['top', 'bottom'], 'all': 'all', 'default': ' top,\tbottom ', 'description': 'Example.'}
        GEN.validate([item])
        self.assertIn('value_or(Flags<Example>{3})', GEN.config_header([item]))
        for changes in ({'choices': []}, {'choices': ['top', 'top']}, {'choices': ['Bad']},
                        {'choices': [f'choice{i}' for i in range(9)]}, {'all': 'top'}, {'all': 'bad-name'}):
            with self.subTest(changes=changes):
                with self.assertRaisesRegex(ValueError, 'window.example: set'):
                    GEN.validate([dict(item, **changes)])

    def test_generated_config_inheritance_at_runtime(self):
        header = self.tmp / 'config_generated.hpp'
        header.write_text(GEN.config_header(SCHEMA))
        binary = self.tmp / 'config-inheritance-tests'
        result = subprocess.run(['c++', '-std=c++23', '-I', str(self.tmp),
                                 str(ROOT / 'tests' / 'ConfigInheritanceTests.cpp'), '-o', str(binary)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_readme_markers_fail(self):
        readme, _ = self.copies()
        readme.write_text(readme.read_text().replace(GEN.BEGIN, ''))
        result = self.run_generator('--readme', str(readme), '--check')
        self.assertEqual(result.returncode, 1)

    def test_commented_lua_text_outside_markers_is_preserved(self):
        _, lua = self.copies()
        original = lua.read_text()
        lua.write_text(original.replace(LUA_BEGIN, LUA_BEGIN + '\n-- stale line', 1))
        result = self.run_generator('--lua', str(lua))
        self.assertEqual(result.returncode, 0, result.stderr)
        text = lua.read_text()
        self.assertNotIn('stale line', text)
        self.assertEqual(text.split(LUA_BEGIN)[0], original.split(LUA_BEGIN)[0])
        self.assertEqual(text.split(LUA_END)[1], original.split(LUA_END)[1])

    def test_nested_lua_edit_inside_region_is_stale(self):
        _, lua = self.copies()
        original = lua.read_text()
        self.assertIn('count = 250,', original)
        lua.write_text(original.replace('count = 250,', 'count = 200,', 1))
        result = self.run_generator('--lua', str(lua), '--check')
        self.assertEqual(result.returncode, 1)


if __name__ == '__main__':
    unittest.main()
