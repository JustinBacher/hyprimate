#!/usr/bin/env python3
"""Relink the clean Ninja build for the private negative control; wrap only Construction's host call."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess as sp
import tempfile


def link(build, output):
    build, output = build.resolve(strict=True), output.resolve()
    if output.exists():
        raise FileExistsError(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    commands = json.loads(sp.check_output(['ninja', '-C', build, '-t', 'compdb']))
    link_command, = (c for c in commands if c['output'] == 'hyprimate.so')
    # CMake's rule also runs a post-build symbol check. Never replay its shell or replace the clean plugin.
    command = shlex.split(link_command['command'])
    start = command.index('-shared')
    compiler = command[2] if command[:2] == [':', '&&'] else command[0]
    args = command[start:command.index('&&', start)]
    args = [a for a in args if not a.startswith('-Wl,--dependency-file=')]
    args[args.index('-o') + 1] = str(output)
    obj, = (arg for arg in args if arg.endswith('/ConstructionRenderer.cpp.o'))
    imports = sp.check_output(['nm', '-u', build / obj], text=True).splitlines()
    symbol, = (line.split()[-1] for line in imports if '13getWorkBufferE' in line)
    cflags = shlex.split(sp.check_output(['pkg-config', '--cflags', 'hyprland'], text=True))
    with tempfile.TemporaryDirectory(prefix='construction-fault-') as temporary:
        fixture, renderer = (Path(temporary) / name for name in ('work.o', 'renderer.o'))
        sp.run([compiler, '-std=c++26', '-O2', '-fPIC', '-fno-gnu-unique', *cflags,
                f'-DCONSTRUCTION_WRAPPED_WORK_BUFFER="__wrap_{symbol}"', '-c',
                Path(__file__).with_name('construction_work_fault.cpp'), '-o', fixture], check=True)
        # An out-of-line inline helper is a weak COMDAT. Give this object's helper
        # a fixture-local identity or the final link can select a clean style's copy.
        original = build / obj
        helpers = [line.split()[0] for line in sp.check_output(
            ['nm', '-P', '--defined-only', original], text=True).splitlines()
            if '15get_work_bufferE' in line.split()[0]]
        if helpers:
            helper, = helpers
            private = Path(temporary) / 'isolated.o'
            sp.run(['objcopy', '--redefine-sym', helper+'=construction_fixture_work_lease',
                    original, private], check=True)
            original = private
        sp.run(['ld', '-r', '--wrap=' + symbol, original, fixture, '-o', renderer], check=True)
        args[args.index(obj)] = str(renderer)
        sp.run([compiler, *args], cwd=build, check=True)


if __name__ == '__main__':
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('build', type=Path)
    cli.add_argument('output', type=Path)
    options = cli.parse_args()
    link(options.build, options.output)
