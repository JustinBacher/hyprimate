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

    def test_supported_roots_and_workspace_shape(self):
        paths = {item['path'] for item in SCHEMA}
        self.assertEqual({path.split('.')[0] for path in paths}, {'window', 'workspace'})
        self.assertEqual({path for path in paths if path.startswith('workspace.options.rotation.')},
                         {'workspace.options.rotation.duration', 'workspace.options.rotation.curve',
                          'workspace.options.rotation.gap_fill_style', 'workspace.options.rotation.gap_fill_color',
                          'workspace.options.rotation.gap_fill_blur',
                          'workspace.options.rotation.keep_overlays_static'})
        items = {item['path']: item for item in SCHEMA}
        self.assertIs(items['workspace.enabled']['default'], False)
        self.assertEqual(items['workspace.style']['choices'], ['rotation', 'planet_hop'])
        self.assertEqual(items['workspace.style']['default'], 'rotation')
        duration = items['workspace.options.rotation.duration']
        self.assertEqual((duration['default'], duration['min']), (600, 0))
        self.assertNotIn('max', duration)
        curve = items['workspace.options.rotation.curve']
        self.assertEqual(curve['default'], 'default')
        self.assertNotIn('choices', curve)
        gap_style = items['workspace.options.rotation.gap_fill_style']
        self.assertEqual(gap_style['choices'], ['morph', 'background', 'wallpaper', 'still'])
        self.assertEqual(gap_style['default'], 'still')
        self.assertEqual(items['workspace.options.rotation.gap_fill_color']['default'], '0xff000000')
        gap_blur = items['workspace.options.rotation.gap_fill_blur']
        self.assertEqual((gap_blur['default'], gap_blur['min'], gap_blur['max']), (0, 0, 64))
        self.assertIs(items['workspace.options.rotation.keep_overlays_static']['default'], False)
        self.assertFalse(any(item.get('optional') or item.get('inherited')
                             for path, item in items.items() if path.startswith('workspace.')))

    def test_workspace_and_window_styles_have_independent_enums_and_validators(self):
        header = GEN.settings_header(SCHEMA) + GEN.config_header(SCHEMA)
        self.assertIn('enum class WorkspaceStyle : std::uint8_t { Rotation, PlanetHop };', header)
        self.assertIn('WorkspaceStyle style{};', header)
        self.assertIn('Style style{};', header)
        window_styles = ['liquid', 'paper', 'cloth', 'swarm', 'bubble', 'smoke', 'burn']
        for path, accepted, rejected in [('workspace.style', ['rotation', 'planet_hop'], window_styles),
                                         ('window.style', window_styles, ['rotation', 'planet_hop']),
                                         ('workspace.options.rotation.gap_fill_style',
                                          ['morph', 'background', 'wallpaper', 'still'], ['rotation', 'invalid'])]:
            item = next(item for item in SCHEMA if item['path'] == path)
            self.assertEqual(item['choices'], accepted)
            registration = header.split(f'values.{item["member"]} = makeShared', 1)[1].split('},});', 1)[0]
            for choice in accepted:
                self.assertIn(f'value == "{choice}"', registration)
            for choice in rejected:
                self.assertNotIn(f'value == "{choice}"', registration)
        self.assertIn('config.workspace.rotation.duration = detail::clamp_float(values.m_workspaceRotationDuration->value(), 0.0F);', header)

    def test_settings_header_is_standalone(self):
        header = self.tmp / 'config_settings_generated.hpp'
        result = self.run_generator('--settings-header', str(header))
        self.assertEqual(result.returncode, 0, result.stderr)
        text = header.read_text()
        self.assertNotIn('SConfigValues', text)
        self.assertNotIn('HyprlandAPI', text)
        result = subprocess.run(
            ['c++', '-std=c++23', '-fsyntax-only', '-x', 'c++', '-I', str(self.tmp), '-'],
            input='#include "config_settings_generated.hpp"\n'
                  'int main() { hyprimate::config::SLiquidSettings settings{}; (void)settings; }\n',
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_workspace_option_enums_are_style_scoped(self):
        rotation = next(item for item in SCHEMA if item['path'] == 'workspace.options.rotation.gap_fill_style')
        other = dict(rotation, path='workspace.options.other.gap_fill_style',
                     member='m_workspaceOtherGapFillStyle', choices=['solid', 'clear'], default='solid')
        header = GEN.settings_header(SCHEMA + [other])
        self.assertIn('enum class RotationGapFillStyle : std::uint8_t { Morph, Background, Wallpaper, Still };', header)
        self.assertIn('enum class OtherGapFillStyle : std::uint8_t { Solid, Clear };', header)
        self.assertIn('inline constexpr std::array WORKSPACE_STYLES{WorkspaceStyle::Rotation, WorkspaceStyle::PlanetHop};', header)
        styles = [dict(item) for item in SCHEMA]
        selection = next(item for item in styles if item['path'] == 'workspace.style')
        selection['choices'] = ['rotation', 'other']
        expanded = GEN.settings_header(styles + [other])
        self.assertIn('inline constexpr std::array WORKSPACE_STYLES{WorkspaceStyle::Rotation, WorkspaceStyle::Other};',
                      expanded)

    def test_planet_hop_contract(self):
        items = {item['path'].removeprefix('workspace.options.planet_hop.'): item
                 for item in SCHEMA if item['path'].startswith('workspace.options.planet_hop.')}
        modes = {
            'window_mode': (['surface', 'floating', 'fade'], 'surface'),
            'overlay_mode': (['world', 'hud', 'fade'], 'fade'),
            'planet_layout': (['focused', 'monitor', 'all', 'solar_system'], 'focused'),
        }
        for name, (choices, default) in modes.items():
            with self.subTest(name=name):
                self.assertEqual(items[name]['choices'], choices)
                self.assertEqual(items[name]['default'], default)
        for name in ('stars_enabled', 'atmosphere_enabled', 'lighting_enabled', 'clouds_enabled',
                     'depth_enabled', 'rotation_enabled'):
            self.assertIs(items[name]['default'], True)
        self.assertEqual(items['duration']['default'], 800)
        self.assertEqual(items['duration']['min'], 0)
        self.assertNotIn('max', items['duration'])
        self.assertEqual(items['curve']['default'], 'linear')
        self.assertEqual(items['depth_source']['default'], '')
        self.assertEqual((items['live_boundary']['min'], items['live_boundary']['default'],
                          items['live_boundary']['max']), (0.08, 0.22, 0.35))
        self.assertEqual((items['elevation']['min'], items['elevation']['default'],
                          items['elevation']['max']), (0, 0.2, 1))
        self.assertEqual((items['sampling_quality']['min'], items['sampling_quality']['default'],
                          items['sampling_quality']['max']), (0, 2, 2))
        header = GEN.settings_header(SCHEMA) + GEN.config_header(SCHEMA)
        for name, (choices, _) in modes.items():
            item = items[name]
            registration = header.split(f'values.{item["member"]} = makeShared', 1)[1].split('},});', 1)[0]
            for choice in choices:
                self.assertIn(f'value == "{choice}"', registration)
            self.assertIn('must be one of:', registration)
        GEN.validate(SCHEMA)

    def test_inheritance_expansion_is_limited_to_window(self):
        items = json.loads(SCHEMA_PATH.read_text())
        extra = {'path': 'workspace.open.options.rotation.duration', 'member': 'm_workspaceOpenRotationDuration',
                 'type': 'float', 'default': 0.6, 'min': 0.1, 'max': 3, 'description': 'Example.'}
        expanded = GEN.expanded_settings(items + [extra])
        actual = next(item for item in expanded if item['path'] == extra['path'])
        self.assertEqual(actual, extra)
        self.assertNotIn('window.options.rotation.duration', {item['path'] for item in expanded})
        with self.assertRaisesRegex(ValueError, 'inherit must be boolean'):
            GEN.validate([dict(extra, inherit=True)])

    def test_paper_live_configuration_contract(self):
        prefix = 'window.options.paper.'
        items = {item['path'][len(prefix):]: item for item in SCHEMA
                 if item['path'].startswith(prefix)}
        defaults = {
            'stiffness': .75, 'inertia': .45, 'settle_damping': .8,
            'move.flutter.air_resistance': .65, 'move.flutter.intensity': .5,
            'move.flutter.frequency': 1.2, 'move.flutter.lift': .3, 'move.flutter.torsion': .4,
            'move.glide.friction': .6, 'move.glide.corner_lift': .25,
            'move.glide.edge_curl': .3, 'move.glide.shadow_separation': .2,
            'resize.fold_intensity': .6, 'resize.curl_intensity': .45, 'resize.crease_density': .5,
        }
        for name, default in defaults.items():
            with self.subTest(name=name):
                item = items[name]
                self.assertEqual(item['type'], 'float')
                self.assertEqual(item['default'], default)
                self.assertEqual((item['min'], item['max']),
                                 (.1, 4) if name == 'move.flutter.frequency' else (0, 1))
                self.assertFalse(item.get('optional') or item.get('inherited'))
                self.assertTrue(item['description'])
        self.assertEqual(items['move.style']['default'], 'flutter')
        self.assertEqual(items['move.style']['choices'], ['flutter', 'glide'])
        self.assertEqual({name for name in items if name.startswith('move.')},
                         {'move.style'} | {name for name in defaults if name.startswith('move.')})
        for direction in ('open', 'close', 'movement'):
            for name in defaults.keys() | {'move.style'}:
                self.assertFalse(any(item['path'] == f'window.{direction}.options.paper.{name}'
                                     for item in SCHEMA))
        header = GEN.settings_header(SCHEMA)
        self.assertIn('enum class PaperMoveStyle : std::uint8_t { Flutter, Glide };', header)
        for declaration in ('SPaperMoveFlutter flutter{};', 'SPaperMoveGlide glide{};',
                            'SPaperMove move{};', 'SPaperResize resize{};',
                            'PaperMoveStyle style{};', 'double airResistance{};',
                            'double cornerLift{};', 'double edgeCurl{};',
                            'double shadowSeparation{};', 'double creaseDensity{};'):
            self.assertIn(declaration, header)
        generated = GEN.config_header(SCHEMA)
        self.assertIn('config.paper.move.flutter.airResistance = detail::clamp_float', generated)
        self.assertIn('config.paper.move.glide.shadowSeparation = detail::clamp_float', generated)
        self.assertIn('config.paper.resize.foldIntensity = detail::clamp_float', generated)
        self.assertIn('config.paper.shared.settleDamping = detail::clamp_float', generated)

    def test_paper_legacy_material_fallbacks(self):
        for direction in ('open', 'close'):
            for legacy, shared in [('paper_stiffness', 'stiffness'), ('settling', 'settle_damping')]:
                item = next(item for item in SCHEMA
                            if item['path'] == f'window.{direction}.options.paper.{legacy}')
                self.assertEqual(item['fallback'], f'window.options.paper.{shared}')
                self.assertTrue(item['inherited'])
        self.assertFalse(any('fallback' in item for item in SCHEMA
                             if item['path'].startswith('window.options.')))
        GEN.validate(SCHEMA)

    def test_invalid_material_fallbacks_are_rejected(self):
        items = json.loads(SCHEMA_PATH.read_text())
        original = next(item for item in items if item['path'] == 'window.open.options.paper.paper_stiffness')
        for fallback in ('window.options.paper.missing', 'window.options.paper.move.style',
                         'window.open.options.paper.settling'):
            with self.subTest(fallback=fallback):
                invalid = [dict(item, fallback=fallback) if item is original else item for item in items]
                with self.assertRaisesRegex(ValueError, 'fallback must name'):
                    GEN.validate(invalid)

    def test_close_options_are_side_and_splash(self):
        paths = {item['path'] for item in SCHEMA}
        self.assertIn('window.close.options.liquid.side', paths)
        self.assertIn('window.close.options.liquid.splash', paths)
        self.assertFalse(any(p.endswith(('close_side', 'close_splash')) for p in paths))

    def test_every_animation_style_setting_has_a_shared_key(self):
        paths = {item['path'] for item in SCHEMA}
        for item in SCHEMA:
            path = item['path']
            if not item.get('inherit', True):
                continue
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
            expected_range = '—'
            if item['type'] in ('int', 'float'):
                expected_range = f'{item["min"]}–{item["max"]}' if 'max' in item else f'≥ {item["min"]}'
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
            overrides = [other for other in SCHEMA if other.get('inherited')
                         and other['path'].split('.options.', 1)[1] == suffix]
            values = {other['default'] for other in overrides}
            with self.subTest(path=item['path']):
                self.assertEqual(item['path'] in active,
                                 len(values) <= 1 and not item.get('natural_timing')
                                 and not any('fallback' in other for other in overrides))

    def test_readme_describes_differing_animation_defaults_and_meanings(self):
        block = GEN.documentation(SCHEMA)
        for style, opening, closing in [('paper', '600', '500'),
                                        ('cloth', '1000', '900'),
                                        ('bubble', '1000', '420')]:
            row = next(line for line in block.splitlines()
                       if line.startswith(f'| `window:options:{style}:duration` |'))
            self.assertIn(f'open `{opening}`', row)
            self.assertIn(f'close `{closing}`', row)
            self.assertIn('Open:', row)
            self.assertIn('Close:', row)
            self.assertNotIn(f'Shared {style} duration setting.', row)
        self.assertIn('window.<animation>.options.<style>', block)
        self.assertIn('clamped to the receiving animation', block)

    def test_window_transition_durations_are_nonnegative_milliseconds_without_a_cap(self):
        defaults = {'burn': (1000, 1500), 'paper': (600, 500), 'cloth': (1000, 900),
                    'bubble': (1000, 420), 'smoke': (1400, 1600),
                    'liquid': (0, 0), 'swarm': (0, 0)}
        items = {item['path']: item for item in SCHEMA}
        for style, durations in defaults.items():
            shared = items[f'window.options.{style}.duration']
            for phase, default in zip(('open', 'close'), durations):
                with self.subTest(style=style, phase=phase):
                    item = items[f'window.{phase}.options.{style}.duration']
                    self.assertEqual(item['default'], default)
                    self.assertEqual(item['min'], 0)
                    self.assertNotIn('max', item)
                    self.assertEqual(shared['min'], 0)
                    self.assertNotIn('max', shared)

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
        settings_header = self.tmp / 'config_settings_generated.hpp'
        result = self.run_generator('--schema', str(schema), '--readme', str(readme), '--lua', str(lua),
                                    '--header', str(header), '--settings-header', str(settings_header))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('#include "config_settings_generated.hpp"', header.read_text())
        self.assertIn('struct SLiquidSettings', settings_header.read_text())
        self.assertNotIn('SConfigValues', settings_header.read_text())
        self.assertNotIn('HyprlandAPI', settings_header.read_text())
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
        (self.tmp / 'config_settings_generated.hpp').write_text(GEN.settings_header(SCHEMA))
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
