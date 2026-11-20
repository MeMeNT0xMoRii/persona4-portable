#!/usr/bin/env python3
"""QA15: fail-closed Python process-mode check for native PSP build stages.

Do not launch original psp_census.py, psp_link.py, psp_hw_debug.py or
psp_make_data.py with Python -O/-OO or PYTHONOPTIMIZE: those source-pinned
build programs still have layout/rebase/source-shim guards implemented as assert.
Only checks the interpreter running THIS invocation, not later processes.
"""
import os
import sys


def inspect(mode, env_value):
    problems = []
    if mode != 0:
        problems.append(f'PYTHON_RUNTIME_OPTIMIZED:{mode}')
    if env_value not in (None, '', '0'):
        problems.append('PYTHONOPTIMIZE_ENV:' + repr(env_value))
    return problems


def main():
    problems = inspect(sys.flags.optimize, os.environ.get('PYTHONOPTIMIZE'))
    for problem in problems:
        print('NO-GO', problem)
    if problems:
        print('Do not invoke original PSP Python builders with assertion checks disabled.')
        return 2
    print('PASS: interpreter optimization flags disabled; NOT proof future builder invocations use the same mode')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
