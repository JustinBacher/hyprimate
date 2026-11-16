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
SCHEMA = json.loads(SCHEMA_PATH.read_text())
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

    def test_registration_keys_and_members(self):
        header = GEN.config_header(SCHEMA)
        for item in REGISTERED:
            self.assertIn(f'values.{item["member"]} = makeShared<', header)
            self.assertIn(f'"{GEN.key(item)}"', header)
        self.assertEqual(len(re.findall(r'= makeShared<', header)), len(REGISTERED))
        self.assertIn('plugin:hyprimate:window:close:options:liquid:splash', header)
        self.assertIn('m_closeSplash', header)

    def test_string_choices_and_float_ranges_are_enforced(self):
        header = GEN.config_header(SCHEMA)
        for item in REGISTERED:
            if item['type'] == 'string':
                self.assertIn(GEN.key(item) + ' must be one of: ' + ', '.join(item['choices']), header)
            if item['type'] == 'float':
                self.assertIn(f'.min = {GEN.float_literal(item["min"])}', header)

    def test_lua_leaves_match_schema_paths(self):
        self.assertEqual(lua_leaf_paths(GEN.lua_config(SCHEMA)), {item['path'] for item in SCHEMA})

    def test_nested_example_leaves_match_schema_paths(self):
        region = nested_region(LUA.read_text())
        self.assertEqual(lua_leaf_paths(region), {item['path'] for item in SCHEMA})

    def test_readme_documents_every_registered_key(self):
        block = GEN.documentation(SCHEMA)
        documented = set(re.findall(r'`(plugin:hyprimate:[a-z_:]+)`', block))
        self.assertEqual(documented, {GEN.key(item) for item in REGISTERED})

    def test_strength_zero_contract_is_documented_in_schema(self):
        strengths = [item for item in SCHEMA if item['path'].endswith('.strength')]
        self.assertEqual(len(strengths), 4)
        for strength in strengths:
            self.assertEqual(strength['min'], 0)
            self.assertIn('zero', strength['description'].lower())

    def test_repository_artifacts_are_current(self):
        result = self.run_generator('--readme', str(README), '--lua', str(LUA), '--check')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_schema_change_regenerates_outputs_and_flags_stale_repo_copies(self):
        items = json.loads(SCHEMA_PATH.read_text())
        next(item for item in items if item['path'] == 'window.movement.options.liquid.strength')['default'] = 0.7
        schema = self.write_schema(items)
        readme, lua = self.copies()
        header = self.tmp / 'config_generated.hpp'
        result = self.run_generator('--schema', str(schema), '--readme', str(readme), '--lua', str(lua),
                                    '--header', str(header))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('strength = 0.7,', lua.read_text())
        self.assertIn('0.7F', header.read_text())
        self.assertIn('strength = 0.7,', readme.read_text())
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

    def test_swarm_header_contract(self):
        header = GEN.config_header(SCHEMA)
        self.assertIn('enum class LoneWindowSides : std::uint8_t { Top = 1, Bottom = 2, Left = 4, Right = 8 };', header)
        self.assertIn('c.swarm.count = detail::clampInt(v.m_swarmCount->value(), 1, 400);', header)
        for direction in ('open', 'close'):
            self.assertIn(f'values.m_{direction}SwarmLoneWindowSides = makeShared<Config::Values::CStringValue>', header)
            self.assertIn(f'c.{direction}.swarm.loneWindowSides = detail::parseLoneWindowSides(', header)

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
        lua.write_text(lua.read_text().replace('strength = 0.65,', 'strength = 0.5,', 1))
        result = self.run_generator('--lua', str(lua), '--check')
        self.assertEqual(result.returncode, 1)


if __name__ == '__main__':
    unittest.main()
