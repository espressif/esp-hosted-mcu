#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""
Check that functions are properly marked as weak symbols.

Usage:
    # Single file, all esp_ functions:
    ./check_weak_functions.py --file path/to/file.c

    # Single file, specific functions:
    ./check_weak_functions.py --file path/to/file.c --functions func1,func2,func3

    # Multiple files (--functions applies to the preceding --file):
    ./check_weak_functions.py --file file1.c --functions a,b --file file2.c --file file3.c --functions c
"""

import re
import sys
import argparse
from pathlib import Path

# All possible weak symbol markers
WEAK_MARKERS = [
    'H_WEAK_REF',
    'WEAK_REF',
    'WEAK',
    '__attribute__((weak))',
]

def check_weak_functions(filepath, target_functions=None, debug=False, verbose=False):
    """
    Check that functions have weak symbol attributes.

    Args:
        filepath: Path to the file to check
        target_functions: List of specific function names to check, or None for all
        debug: Enable debug output
        verbose: Print success messages

    Returns:
        bool: True if all checks pass, False otherwise
    """

    if not Path(filepath).exists():
        print(f"❌ ERROR: {filepath} not found.", file=sys.stderr)
        return False

    with open(filepath, 'r') as f:
        content = f.read()

    violations = []
    found_functions = {}

    # Pattern to match function definitions (not calls)
    # A function definition has the opening brace { on the same or next line
    func_def_pattern = r'([^\n;]*?)\b(\w+)\s*\([^)]*\)\s*(?:\n\s*)?\{'

    for match in re.finditer(func_def_pattern, content, re.MULTILINE):
        function_name = match.group(2)

        # Only check functions starting with esp_
        if not function_name.startswith('esp_'):
            continue

        # Find the line number
        start_pos = match.start()
        line_num = content[:start_pos].count('\n') + 1

        # Get the full line(s) for this function definition
        declaration = match.group(0)

        # Check if any weak marker is present in the declaration
        has_weak_marker = any(marker in declaration for marker in WEAK_MARKERS)
        found_marker = None
        if has_weak_marker:
            for marker in WEAK_MARKERS:
                if marker in declaration:
                    found_marker = marker
                    break

        if debug:
            marker_str = found_marker if has_weak_marker else "NONE"
            print(f"DEBUG: Line {line_num}: {function_name}()")
            print(f"       Marker: {marker_str}")
            print(f"       Declaration: {declaration[:100]}...")
            print()

        found_functions[function_name] = {
            'line': line_num,
            'has_weak': has_weak_marker,
            'marker': found_marker
        }

        # If checking specific functions, skip others
        if target_functions and function_name not in target_functions:
            continue

        if not has_weak_marker:
            violations.append(f"  Line {line_num}: {function_name}() - Missing weak marker")

    # Check if all target functions were found
    if target_functions:
        missing = set(target_functions) - set(found_functions.keys())
        if missing:
            print(f"❌ ERROR: The following functions were not found in {filepath}:", file=sys.stderr)
            for func in sorted(missing):
                print(f"  - {func}()", file=sys.stderr)
            print(file=sys.stderr)
            return False

    # Report results
    if violations:
        print(f"❌ ERROR: Found function definitions without weak markers in {filepath}:", file=sys.stderr)
        print('\n'.join(violations), file=sys.stderr)
        print(f"\nAccepted weak markers: {', '.join(WEAK_MARKERS)}", file=sys.stderr)
        return False

    # Success message (only if verbose or debug)
    if verbose or debug:
        if target_functions:
            checked_count = len([f for f in target_functions if f in found_functions])
            if checked_count > 0:
                print(f"✅ Checked {checked_count} function(s) in {filepath}")
                for func in target_functions:
                    if func in found_functions:
                        info = found_functions[func]
                        marker_display = info['marker'] if info['marker'] else "NO MARKER"
                        print(f"   Line {info['line']}: {func}() - {marker_display}")
        else:
            total = len(found_functions)
            print(f"✅ All {total} function(s) in {filepath} are properly marked as weak")

    return True

def main():
    parser = argparse.ArgumentParser(
        description='Check that functions are properly marked as weak symbols',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )

    parser.add_argument(
        '--file', '-f',
        dest='files',
        metavar='FILE',
        help='File to check. Can be repeated. Each --file may be followed by --functions.'
    )

    parser.add_argument(
        '--functions', '-F',
        help='Comma-separated list of specific function names to check'
    )

    parser.add_argument(
        '--verbose', '-v',
        action='store_true',
        help='Print success messages'
    )

    parser.add_argument(
        '--debug', '-d',
        action='store_true',
        help='Debug output'
    )

    # Don't use parse_args() — walk sys.argv manually so we can pair
    # each --file with the --functions that immediately follows it (if any).
    checks = []
    current_file = None
    verbose = False
    debug = False

    i = 1
    while i < len(sys.argv):
        arg = sys.argv[i]
        if arg in ('--file', '-f'):
            if current_file is not None:
                # previous --file had no --functions, check all
                checks.append((current_file, None))
            current_file = sys.argv[i + 1]
            i += 2
        elif arg in ('--functions', '-F'):
            if current_file is None:
                print("❌ ERROR: --functions must follow --file", file=sys.stderr)
                sys.exit(1)
            funcs = [f.strip() for f in sys.argv[i + 1].split(',') if f.strip()]
            checks.append((current_file, funcs))
            current_file = None
            i += 2
        elif arg in ('--verbose', '-v'):
            verbose = True
            i += 1
        elif arg in ('--debug', '-d'):
            debug = True
            i += 1
        else:
            i += 1

    # flush last --file if it had no --functions
    if current_file is not None:
        checks.append((current_file, None))

    if not checks:
        print("❌ ERROR: At least one --file is required.", file=sys.stderr)
        parser.print_help()
        sys.exit(1)

    if debug:
        for filepath, funcs in checks:
            func_str = ', '.join(funcs) if funcs else 'all esp_ functions'
            print(f"Checking: {filepath} ({func_str})")
        print()

    all_passed = True
    for filepath, target_functions in checks:
        if not check_weak_functions(filepath, target_functions, debug=debug, verbose=verbose):
            all_passed = False

    if not all_passed:
        sys.exit(1)

if __name__ == '__main__':
    main()
