#!/usr/bin/env python3
"""Generate the plugin configuration header, the nested Lua example and the README section from config/settings.json.

The header holds the settings handles (SConfigValues), their registration, the typed configuration tree (SConfig)
and readConfig(), which clamps every float to its schema range and maps every string choice to an enum class.
Open and close share one STransitionConfig shape; direction-only settings occupy the shared fields of the
direction that owns them and keep their zero value in the other direction.
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
KINDS = {'boolean': 'CBoolValue', 'int': 'CIntValue', 'float': 'CFloatValue', 'string': 'CStringValue',
         'set': 'CStringValue', 'color': 'CColorValue'}
CTYPES = {'boolean': 'bool', 'int': 'int', 'float': 'double', 'color': 'std::uint32_t'}
SEGMENT = re.compile(r'[a-z][a-z0-9_]*\Z')
MEMBER = re.compile(r'm_[A-Za-z][A-Za-z0-9]*\Z')
COLOR = re.compile(r'0x[0-9a-fA-F]{8}\Z')
TRANSITIONS = ('open', 'close')


def literal(value):
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, str):
        return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'
    return str(value)


def float_literal(value):
    text = str(value)
    return (text if '.' in text or 'e' in text.lower() else text + '.0') + 'F'


def pascal(text):
    return ''.join(part[:1].upper() + part[1:] for part in text.split('_'))


def camel(text):
    name = pascal(text)
    return name[:1].lower() + name[1:]


def key(item):
    return KEY_PREFIX + item['path'].replace('.', ':')


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
        member = item['member']
        if not MEMBER.match(member) or member in members:
            raise ValueError(f'{path}: invalid or duplicate member {member!r}')
        if kind not in KINDS:
            raise ValueError(f'{path}: unsupported type {kind!r}')
        default = item['default']
        if kind == 'boolean' and not isinstance(default, bool):
            raise ValueError(f'{path}: boolean default must be true or false')
        if kind == 'int':
            low, high = item['min'], item['max']
            if not all(type(value) is int for value in (low, high, default)) or not low <= default <= high:
                raise ValueError(f'{path}: int default must lie within integer min..max')
        if kind == 'float':
            low, high = item['min'], item['max']
            if not (is_number(low) and is_number(high) and is_number(default)) or not low <= default <= high:
                raise ValueError(f'{path}: float default must lie within finite min..max')
        if kind == 'string':
            choices = item['choices']
            if not isinstance(choices, list) or not choices or not all(isinstance(c, str) for c in choices) or default not in choices:
                raise ValueError(f'{path}: string default must be one of the non-empty string choices')
        if kind == 'set':
            choices, all_keyword = item['choices'], item['all']
            if (not isinstance(choices, list) or not 1 <= len(choices) <= 8
                    or not all(isinstance(c, str) and SEGMENT.fullmatch(c) for c in choices)
                    or len(set(choices)) != len(choices)):
                raise ValueError(f'{path}: set choices must be 1..8 distinct valid names')
            if not isinstance(all_keyword, str) or not SEGMENT.fullmatch(all_keyword) or all_keyword in choices:
                raise ValueError(f'{path}: set all must be a distinct valid name')
            if not isinstance(default, str) or not all(token.strip(' \t') in choices + [all_keyword]
                                                       for token in default.split(',')):
                raise ValueError(f'{path}: set default must be a comma-separated list of the choices')
        if kind == 'color' and not (isinstance(default, str) and COLOR.match(default)):
            raise ValueError(f'{path}: color default must be 0x followed by eight hex digits')
        members.add(member)
    for path in paths:
        parts = path.split('.')
        for depth in range(1, len(parts)):
            if '.'.join(parts[:depth]) in paths:
                raise ValueError(f'{path!r} conflicts with a setting at {".".join(parts[:depth])!r}')
    enums(settings)


class _Node(dict):
    """Interior table of the nested Lua configuration tree; leaves are schema items."""


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
        else:
            value = child['default'] if child['type'] == 'color' else literal(child['default'])
            lines.append(f'{pad}{name} = {value},')
    return lines


def lua_config(settings):
    body = lua_fields(tree(settings), 3)
    return '\n'.join(['hl.config({', '    decoration = { wobble = { enabled = false } },', '    plugin = {',
                      '        ["liquid_glass"] = {', *body, '        },', '    },', '})'])


def config_segments(item):
    """Path under window, without the Lua-only 'options' level; these are the SConfig field names."""
    return [part for part in item['path'].split('.')[1:] if part != 'options']


def enum_name(item):
    return pascal(item['path'].split('.')[-1])


def enums(settings):
    found = {}
    for item in settings:
        if item['type'] not in ('string', 'set'):
            continue
        name = enum_name(item)
        if name in found and (found[name]['type'] != item['type'] or found[name]['choices'] != item['choices']
                              or (item['type'] == 'set' and found[name]['all'] != item['all'])):
            raise ValueError(f'{item["path"]}: enum {name} is already declared with other choices or type')
        found.setdefault(name, item)
    return found


def type_name(segments):
    if not segments:
        return 'SConfig'
    if segments[0] in TRANSITIONS:
        return 'STransitionConfig' if len(segments) == 1 else 'STransition' + ''.join(pascal(s) for s in segments[1:])
    return 'S' + ''.join(pascal(s) for s in segments)


def struct_types(settings):
    """type name -> ordered {field: (kind, type)}; open and close merge into one STransitionConfig shape."""
    types = {}
    for item in settings:
        segments = config_segments(item)
        for depth in range(len(segments)):
            fields = types.setdefault(type_name(segments[:depth]), {})
            field = segments[depth]
            if depth == len(segments) - 1:
                typ = (enum_name(item) if item['type'] == 'string' else
                       f'Flags<{enum_name(item)}>' if item['type'] == 'set' else CTYPES[item['type']])
                fields[field] = ('leaf', typ)
            else:
                fields[field] = ('node', type_name(segments[:depth + 1]))
    return types


def emit_struct(types, name, emitted, out):
    if name in emitted:
        return
    emitted.add(name)
    for kind, typ in types[name].values():
        if kind == 'node':
            emit_struct(types, typ, emitted, out)
    out.append(f'struct {name} {{')
    for field, (kind, typ) in types[name].items():
        out.append(f'    {typ} {camel(field)}{{}};')
    out.append('};')
    out.append('')


def config_header(settings):
    out = ['// Generated from config/settings.json; do not edit.', '#pragma once', '',
           '#include <algorithm>', '#include <cmath>', '#include <cstdint>', '#include <expected>',
           '#include <optional>', '#include <stdexcept>', '#include <string>', '', 'namespace liquid_glass::config {', '']
    enum_items = enums(settings)
    if any(item['type'] == 'set' for item in enum_items.values()):
        out.append('template <class E> struct Flags {')
        out.append('    std::uint8_t bits = 0;')
        out.append('    constexpr bool has(E v) const noexcept { return (bits & static_cast<std::uint8_t>(v)) != 0; }')
        out.append('    constexpr bool operator==(const Flags &) const = default;')
        out.append('};')
        out.append('')
    for name, item in enum_items.items():
        choices = (', '.join(f'{pascal(c)} = {1 << i}' for i, c in enumerate(item['choices']))
                   if item['type'] == 'set' else ', '.join(pascal(c) for c in item['choices']))
        out.append(f'enum class {name} : std::uint8_t {{ {choices} }};')
    out.append('')
    out.append('namespace detail {')
    out.append('// Non-finite values read as the schema minimum, which turns a strength or drain off.')
    out.append('inline double clampFloat(float value, double low, double high) noexcept {')
    out.append('    return std::isfinite(value) ? std::clamp(static_cast<double>(value), low, high) : low;')
    out.append('}')
    out.append('')
    out.append('inline int clampInt(long long value, long long low, long long high) noexcept {')
    out.append('    return static_cast<int>(std::clamp(value, low, high));')
    out.append('}')
    out.append('')
    for name, item in enum_items.items():
        if item['type'] == 'set':
            out.append(f'inline std::optional<Flags<{name}>> parse{name}(const std::string& text) noexcept {{')
            out.append(f'    Flags<{name}> result;')
            out.append('    std::size_t start = 0;')
            out.append('    while (true) {')
            out.append("        const auto end = text.find(',', start);")
            out.append('        const auto first = text.find_first_not_of(" \\t", start);')
            out.append('        if (first == std::string::npos || (end != std::string::npos && first >= end)) return std::nullopt;')
            out.append('        const auto last = text.find_last_not_of(" \\t", end == std::string::npos ? end : end - 1);')
            out.append('        const auto length = last - first + 1;')
            out.append(f'        if (text.compare(first, length, {literal(item["all"])}) == 0) result.bits = {(1 << len(item["choices"])) - 1};')
            for i, choice in enumerate(item['choices']):
                out.append(f'        else if (text.compare(first, length, {literal(choice)}) == 0) result.bits |= {1 << i};')
            out.append('        else return std::nullopt;')
            out.append('        if (end == std::string::npos) break;')
            out.append('        start = end + 1;')
            out.append('    }')
            out.append('    return result;')
            out.append('}')
            out.append('')
            continue
        out.append(f'inline {name} parse{name}(const std::string& text) noexcept {{')
        for choice in item['choices']:
            out.append(f'    if (text == {literal(choice)}) return {name}::{pascal(choice)};')
        out.append(f'    return {name}::{pascal(item["default"])};')
        out.append('}')
        out.append('')
    out.append('} // namespace detail')
    out.append('')
    types = struct_types(settings)
    emitted = set()
    emit_struct(types, 'SConfig', emitted, out)
    out.append('struct SConfigValues {')
    for item in settings:
        out.append(f'    SP<Config::Values::{KINDS[item["type"]]}> {item["member"]};')
    out.append('};')
    out.append('')
    out.append('inline void registerConfigValues(HANDLE handle, SConfigValues& values) {')
    for item in settings:
        member, kind = item['member'], item['type']
        name = key(item)
        desc = literal(item['description'])
        default = item['default']
        cls = KINDS[kind]
        value = float_literal(default) if kind == 'float' else (default if kind == 'color' else literal(default))
        if kind in ('int', 'float'):
            if kind == 'float':
                options = f'.min = {float_literal(item["min"])}, .max = {float_literal(item["max"])}'
                options_type = 'SFloatValueOptions'
            else:
                options = f'.min = {item["min"]}, .max = {item["max"]}'
                options_type = 'SIntValueOptions'
            out.append(f'    values.{member} = makeShared<Config::Values::{cls}>({literal(name)}, {desc}, {value},')
            out.append(f'        Config::Values::{options_type}{{{options}}});')
        elif kind in ('string', 'set'):
            if kind == 'string':
                conditions = ' || '.join(f'value == {literal(choice)}' for choice in item['choices'])
                error = name + ' must be one of: ' + ', '.join(item['choices'])
            else:
                conditions = f'detail::parse{enum_name(item)}(value)'
                error = name + ' must be a comma-separated list of: ' + ', '.join(item['choices'] + [item['all']])
            out.append(f'    values.{member} = makeShared<Config::Values::{cls}>({literal(name)}, {desc}, {literal(default)},')
            out.append('        Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {')
            out.append(f'            if ({conditions}) return {{}};')
            out.append(f'            return std::unexpected(std::string({literal(error)}));')
            out.append('        }});')
        else:
            out.append(f'    values.{member} = makeShared<Config::Values::{cls}>({literal(name)}, {desc}, {value});')
    out.append('    if (' + '\n     || '.join(f'!HyprlandAPI::addConfigValueV2(handle, values.{x["member"]})' for x in settings) + ')')
    out.append('        throw std::runtime_error("[liquid-glass] failed to register configuration values");')
    out.append('}')
    out.append('')
    out.append('inline SConfig readConfig(const SConfigValues& v) {')
    out.append('    SConfig c;')
    for item in settings:
        target = 'c.' + '.'.join(camel(s) for s in config_segments(item))
        source = f'v.{item["member"]}->value()'
        if item['type'] == 'int':
            value = f'detail::clampInt({source}, {item["min"]}, {item["max"]})'
        elif item['type'] == 'set':
            tokens = [token.strip(' \t') for token in item['default'].split(',')]
            bits = ((1 << len(item['choices'])) - 1 if item['all'] in tokens else
                    sum(1 << i for i, choice in enumerate(item['choices']) if choice in tokens))
            value = f'detail::parse{enum_name(item)}({source}).value_or(Flags<{enum_name(item)}>{{{bits}}})'
        elif item['type'] == 'float':
            value = f'detail::clampFloat({source}, {float_literal(item["min"])}, {float_literal(item["max"])})'
        elif item['type'] == 'string':
            value = f'detail::parse{enum_name(item)}({source})'
        elif item['type'] == 'color':
            value = f'static_cast<std::uint32_t>({source})'
        else:
            value = source
        out.append(f'    {target} = {value};')
    out.append('    return c;')
    out.append('}')
    out.append('')
    out.append('} // namespace liquid_glass::config')
    return '\n'.join(out) + '\n'


def documentation(settings):
    lines = ['After the first load has registered the settings, add this Lua configuration:', '', '```lua',
             lua_config(settings), '```', '',
             'Hyprland 0.57 normalizes hyphens to underscores in Lua config names: use `liquid_glass` in `hl.config`, while the plugin name and `hyprctl` keys remain `liquid-glass` / `plugin:liquid-glass:*`. Each nested Lua table maps to colon-separated segments of the key, for example `window.movement.options.liquid.viscosity` is `plugin:liquid-glass:window:movement:options:liquid:viscosity`.', '',
             '| Setting | Type / default | Meaning |', '|---|---|---|']
    for item in settings:
        kind = item['type']
        default = item['default'] if kind == 'color' else literal(item['default'])
        meaning = item['description']
        if kind == 'float':
            meaning += f' Finite values clamped to {item["min"]}–{item["max"]}.'
        if kind == 'int':
            meaning += f' Clamped to {item["min"]}–{item["max"]}.'
        if kind == 'set':
            choices = ', '.join('`' + x + '`' for x in item['choices'])
            meaning += f' Comma-separated list of: {choices}; `{item["all"]}` selects every one and overrides the rest.'
        if kind == 'string':
            meaning += ' Allowed: ' + ', '.join('`' + x + '`' for x in item['choices']) + '.'
        lines.append(f'| `{key(item)}` | {kind} / `{default}` | {meaning} |')
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
    parser.add_argument('--header', type=Path, help='generated configuration header')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if not (args.readme or args.lua or args.header):
        parser.error('specify --readme, --lua, or --header')
    settings = json.loads(args.schema.read_text())
    validate(settings)
    outputs = []
    if args.header:
        outputs.append((args.header, config_header(settings)))
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
