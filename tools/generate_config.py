#!/usr/bin/env python3
"""Generate plugin registration and the README from the configuration schema."""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BEGIN = '<!-- BEGIN GENERATED CONFIGURATION -->'
END = '<!-- END GENERATED CONFIGURATION -->'


def literal(value):
    if isinstance(value, bool):
        return str(value).lower()
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    return str(value)


def float_literal(value):
    text = str(value)
    return (text if '.' in text or 'e' in text.lower() else text + '.0') + 'F'


def registration(settings):
    lines = ['// Generated from config/settings.json; do not edit.']
    for item in settings:
        name, member, kind = item['name'], item['member'], item['type']
        key = 'plugin:liquid-glass:' + name
        desc = literal(item['description'])
        default = item['default']
        cls = {'boolean': 'CBoolValue', 'float': 'CFloatValue', 'string': 'CStringValue', 'color': 'CColorValue'}[kind]
        value = float_literal(default) if kind == 'float' else (default if kind == 'color' else literal(default))
        lines.append(f'{member} = makeShared<Config::Values::{cls}>({literal(key)}, {desc}, {value}' + (',' if kind in ('float', 'string') else ');'))
        if kind == 'float':
            lines.append(f'    Config::Values::SFloatValueOptions{{.min = {float_literal(item["min"])}, .max = {float_literal(item["max"])}}});')
        elif kind == 'string':
            conditions = ' || '.join(f'value == {literal(choice)}' for choice in item['choices'])
            error = key + ' must be one of: ' + ', '.join(item['choices'])
            lines.extend(['    Config::Values::SStringValueOptions{.validator = [](const Config::STRING& value) -> std::expected<void, std::string> {',
                          f'        if ({conditions}) return {{}};',
                          f'        return std::unexpected(std::string({literal(error)}));', '    }});'])
    lines.append('if (' + '\n    || '.join('!HyprlandAPI::addConfigValueV2(m_handle, ' + x['member'] + ')' for x in settings) + ')')
    lines.append('    throw std::runtime_error("[liquid-glass] failed to register configuration values");')
    return '\n'.join(lines) + '\n'


def documentation(settings):
    lines = ['After the first load has registered the settings, add this Lua configuration:', '', '```lua', 'hl.config({',
             '    decoration = { wobble = { enabled = false } },', '    plugin = {', '        ["liquid_glass"] = {']
    for item in settings:
        value = item['default'] if item['type'] == 'color' else literal(item['default'])
        lines.append(f'            {item["name"]} = {value},')
    lines += ['        },', '    },', '})', '```', '',
              'Hyprland 0.57 normalizes hyphens to underscores in Lua config names: use `liquid_glass` in `hl.config`, while the plugin name and `hyprctl` keys remain `liquid-glass` / `plugin:liquid-glass:*`.', '',
              '| Setting | Type / default | Meaning |', '|---|---|---|']
    for item in settings:
        kind = item['type']
        default = item['default'] if kind == 'color' else literal(item['default'])
        meaning = item['description']
        if kind == 'float':
            meaning += f' Finite values clamped to {item["min"]}–{item["max"]}.'
        if kind == 'string':
            meaning += ' Allowed: ' + ', '.join('`' + x + '`' for x in item['choices']) + '.'
        lines.append(f'| `plugin:liquid-glass:{item["name"]}` | {kind} / `{default}` | {meaning} |')
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--schema', type=Path, default=ROOT / 'config/settings.json')
    parser.add_argument('--readme', type=Path)
    parser.add_argument('--header', type=Path)
    parser.add_argument('--constants', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if not (args.readme or args.header or args.constants):
        parser.error('specify --readme, --header, or --constants')
    settings = json.loads(args.schema.read_text())
    outputs = []
    if args.header:
        outputs.append((args.header, registration(settings)))
    if args.constants:
        strength = next(item for item in settings if item['name'] == 'strength')
        outputs.append((args.constants, f'// Generated from config/settings.json; do not edit.\nconstexpr double MAX_STRENGTH = {float(strength["max"])};\n'))
    if args.readme:
        text = args.readme.read_text()
        if text.count(BEGIN) != 1 or text.count(END) != 1 or text.index(BEGIN) >= text.index(END):
            raise ValueError('README must contain exactly one ordered pair of configuration markers')
        start = text.index(BEGIN) + len(BEGIN)
        end = text.index(END)
        outputs.append((args.readme, text[:start] + '\n' + documentation(settings) + '\n' + text[end:]))
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
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f'configuration generation failed: {error}', file=sys.stderr)
        sys.exit(1)
