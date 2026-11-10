#!/usr/bin/env python3
"""Generate plugin registration, constants, the nested Lua example and the README from the configuration schema.

Each schema item has a dotted `path` (for example `window.movement.options.liquid.viscosity`). The registered
key is `plugin:liquid-glass:` followed by the path with dots replaced by colons; Lua tables mirror the path.
A `table` item is an empty Lua table in the layout: it registers no setting and has no member or default.
"""
import argparse
import json
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BEGIN = '<!-- BEGIN GENERATED CONFIGURATION -->'
END = '<!-- END GENERATED CONFIGURATION -->'
LUA_BEGIN = '-- BEGIN GENERATED PLUGIN CONFIG'
LUA_END = '-- END GENERATED PLUGIN CONFIG'
KEY_PREFIX = 'plugin:liquid-glass:'
KINDS = {'boolean': 'CBoolValue', 'float': 'CFloatValue', 'string': 'CStringValue', 'color': 'CColorValue'}
SEGMENT = re.compile(r'[a-z][a-z0-9_]*\Z')
MEMBER = re.compile(r'm_[A-Za-z][A-Za-z0-9]*\Z')
COLOR = re.compile(r'0x[0-9a-fA-F]{8}\Z')


def literal(value):
    if isinstance(value, bool):
        return str(value).lower()
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    return str(value)


def float_literal(value):
    text = str(value)
    return (text if '.' in text or 'e' in text.lower() else text + '.0') + 'F'


def key(item):
    return KEY_PREFIX + item['path'].replace('.', ':')


def registered(settings):
    return [item for item in settings if item['type'] != 'table']


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def validate(settings):
    if not isinstance(settings, list) or not settings:
        raise ValueError('schema must be a non-empty list')
    paths, members = set(), set()
    for item in settings:
        path, kind = item['path'], item['type']
        if not all(SEGMENT.match(part) for part in path.split('.')):
            raise ValueError(f'invalid setting path {path!r}')
        if path in paths:
            raise ValueError(f'duplicate setting path {path!r}')
        if not isinstance(item['description'], str) or not item['description'].strip():
            raise ValueError(f'{path}: description must be non-empty text')
        paths.add(path)
        if kind == 'table':
            if 'member' in item or 'default' in item:
                raise ValueError(f'{path}: table entries register nothing and take no member or default')
            continue
        member = item['member']
        if not MEMBER.match(member) or member in members:
            raise ValueError(f'{path}: invalid or duplicate member {member!r}')
        if kind not in KINDS:
            raise ValueError(f'{path}: unsupported type {kind!r}')
        default = item['default']
        if kind == 'boolean' and not isinstance(default, bool):
            raise ValueError(f'{path}: boolean default must be true or false')
        if kind == 'float':
            low, high = item['min'], item['max']
            if not (is_number(low) and is_number(high) and is_number(default)) or not low <= default <= high:
                raise ValueError(f'{path}: float default must lie within finite min..max')
        if kind == 'string':
            choices = item['choices']
            if not isinstance(choices, list) or not choices or not all(isinstance(c, str) for c in choices) or default not in choices:
                raise ValueError(f'{path}: string default must be one of the non-empty string choices')
        if kind == 'color' and not (isinstance(default, str) and COLOR.match(default)):
            raise ValueError(f'{path}: color default must be 0x followed by eight hex digits')
        members.add(member)
    for path in paths:
        parts = path.split('.')
        for depth in range(1, len(parts)):
            if '.'.join(parts[:depth]) in paths:
                raise ValueError(f'{path!r} conflicts with a setting at {".".join(parts[:depth])!r}')


class _Node(dict):
    """Interior table of the nested configuration tree; leaves are schema items."""


def tree(settings):
    root = _Node()
    for item in settings:
        *parents, leaf = item['path'].split('.')
        node = root
        for name in parents:
            node = node.setdefault(name, _Node())
        node[leaf] = item
    return root


def lua_fields(node, depth):
    pad = '    ' * depth
    lines = []
    for name, child in node.items():
        if isinstance(child, _Node):
            lines.append(f'{pad}{name} = {{')
            lines.extend(lua_fields(child, depth + 1))
            lines.append(f'{pad}}},')
        elif child['type'] == 'table':
            lines.append(f'{pad}{name} = {{}},')
        else:
            value = child['default'] if child['type'] == 'color' else literal(child['default'])
            lines.append(f'{pad}{name} = {value},')
    return lines


def lua_config(settings):
    body = lua_fields(tree(settings), 3)
    return '\n'.join(['hl.config({', '    decoration = { wobble = { enabled = false } },', '    plugin = {',
                      '        ["liquid_glass"] = {', *body, '        },', '    },', '})'])


def registration(settings):
    settings = registered(settings)
    lines = ['// Generated from config/settings.json; do not edit.']
    for item in settings:
        member, kind = item['member'], item['type']
        name = key(item)
        desc = literal(item['description'])
        default = item['default']
        cls = KINDS[kind]
        value = float_literal(default) if kind == 'float' else (default if kind == 'color' else literal(default))
        lines.append(f'{member} = makeShared<Config::Values::{cls}>({literal(name)}, {desc}, {value}' + (',' if kind in ('float', 'string') else ');'))
        if kind == 'float':
            lines.append(f'    Config::Values::SFloatValueOptions{{.min = {float_literal(item["min"])}, .max = {float_literal(item["max"])}}});')
        elif kind == 'string':
            conditions = ' || '.join(f'value == {literal(choice)}' for choice in item['choices'])
            error = name + ' must be one of: ' + ', '.join(item['choices'])
            lines.extend(['    Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {',
                          f'        if ({conditions}) return {{}};',
                          f'        return std::unexpected(std::string({literal(error)}));', '    }});'])
    lines.append('if (' + '\n    || '.join('!HyprlandAPI::addConfigValueV2(m_handle, ' + x['member'] + ')' for x in settings) + ')')
    lines.append('    throw std::runtime_error("[liquid-glass] failed to register configuration values");')
    return '\n'.join(lines) + '\n'


def documentation(settings):
    tables = [item for item in settings if item['type'] == 'table']
    lines = ['After the first load has registered the settings, add this Lua configuration:', '', '```lua',
             lua_config(settings), '```', '',
             'Hyprland 0.57 normalizes hyphens to underscores in Lua config names: use `liquid_glass` in `hl.config`, while the plugin name and `hyprctl` keys remain `liquid-glass` / `plugin:liquid-glass:*`. Each nested Lua table maps to colon-separated segments of the key, for example `window.movement.options.liquid.viscosity` is `plugin:liquid-glass:window:movement:options:liquid:viscosity`.', '',
             '| Setting | Type / default | Meaning |', '|---|---|---|']
    for item in registered(settings):
        kind = item['type']
        default = item['default'] if kind == 'color' else literal(item['default'])
        meaning = item['description']
        if kind == 'float':
            meaning += f' Finite values clamped to {item["min"]}–{item["max"]}.'
        if kind == 'string':
            meaning += ' Allowed: ' + ', '.join('`' + x + '`' for x in item['choices']) + '.'
        lines.append(f'| `{key(item)}` | {kind} / `{default}` | {meaning} |')
    if tables:
        lines.extend(['', 'Empty Lua tables in the layout (no settings registered):', ''])
        lines.extend(f'- `{item["path"]}`: {item["description"]}' for item in tables)
    return '\n'.join(lines)


def lua_file_block(settings):
    return '\n'.join(('-- ' + line).rstrip() for line in lua_config(settings).split('\n'))


def replace_region(text, begin, end, body, label):
    if text.count(begin) != 1 or text.count(end) != 1 or text.index(begin) >= text.index(end):
        raise ValueError(f'{label} must contain exactly one ordered pair of configuration markers')
    start = text.index(begin) + len(begin)
    return text[:start] + '\n' + body + '\n' + text[text.index(end):]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--schema', type=Path, default=ROOT / 'config/settings.json')
    parser.add_argument('--readme', type=Path)
    parser.add_argument('--lua', type=Path, help='file containing a commented generated plugin block between markers')
    parser.add_argument('--header', type=Path)
    parser.add_argument('--constants', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if not (args.readme or args.lua or args.header or args.constants):
        parser.error('specify --readme, --lua, --header, or --constants')
    settings = json.loads(args.schema.read_text())
    validate(settings)
    outputs = []
    if args.header:
        outputs.append((args.header, registration(settings)))
    if args.constants:
        strength = next(item for item in settings if item['path'] == 'window.movement.options.liquid.strength')
        outputs.append((args.constants, f'// Generated from config/settings.json; do not edit.\nconstexpr double MAX_STRENGTH = {float(strength["max"])};\n'))
    if args.readme:
        text = args.readme.read_text()
        outputs.append((args.readme, replace_region(text, BEGIN, END, documentation(settings), 'README')))
    if args.lua:
        text = args.lua.read_text()
        outputs.append((args.lua, replace_region(text, LUA_BEGIN, LUA_END, lua_file_block(settings), str(args.lua))))
    stale = False
    for path, content in outputs:
        if not path.exists() or path.read_text() != content:
            if args.check:
                print(f'{path} is stale; regenerate configuration documentation', file=sys.stderr)
                stale = True
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
    return int(stale)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, TypeError, StopIteration) as error:
        print(f'configuration generation failed: {error}', file=sys.stderr)
        sys.exit(1)
