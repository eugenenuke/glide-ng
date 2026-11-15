#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys
import time


class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    BOLD = '\033[1m'
    END = '\033[0m'


def run_command(cmd, env_override=None, env_remove=None):
    env = os.environ.copy()
    if env_remove:
        for key in env_remove:
            env.pop(key, None)
    if env_override:
        env.update(env_override)

    start_time = time.time()
    process = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )
    stdout, stderr = process.communicate()
    elapsed = time.time() - start_time

    return (
        process.returncode == 0,
        stdout.decode('utf-8', errors='replace'),
        stderr.decode('utf-8', errors='replace'),
        elapsed,
    )


def find_binary(name):
    for candidate in (os.path.join('build', 'bin', name), os.path.join('bin', name)):
        if os.path.exists(candidate):
            return candidate
    return None


def main():
    parser = argparse.ArgumentParser(description="Glide Wrapper Catch2 Unit Test Suite Runner")
    parser.add_argument(
        '--backend', '-b',
        choices=['software', 'sw', 'vulkan', 'vk', 'opengl_es', 'gles', 'goes'],
        help="Filter by a specific backend",
    )
    parser.add_argument(
        '--suite', '-s',
        choices=['all', 'g2', 'g3'],
        default='all',
        help="Select which Catch2 test suite(s) to run (default: all)",
    )
    parser.add_argument(
        '--windowed',
        action='store_true',
        help="Disable GLIDE_FORCE_NO_WINDOW=1 (default runs headlessly)",
    )
    args = parser.parse_args()

    backend_map = {
        'software': 'software', 'sw': 'software',
        'vulkan': 'vulkan', 'vk': 'vulkan',
        'opengl_es': 'opengl_es', 'gles': 'opengl_es', 'goes': 'opengl_es',
    }
    backends = [backend_map[args.backend]] if args.backend else ['software', 'vulkan', 'opengl_es']

    requested_suites = []
    if args.suite in ('all', 'g2'):
        requested_suites.append(('Glide2x', 'Glide2xWrapperTests'))
    if args.suite in ('all', 'g3'):
        requested_suites.append(('Glide3x', 'Glide3xWrapperTests'))

    suites = []
    for label, bin_name in requested_suites:
        path = find_binary(bin_name)
        if not path:
            print(
                f"{Colors.RED}{Colors.BOLD}[ERROR]{Colors.END} "
                f"Catch2 binary '{bin_name}' not found! Please build the project first."
            )
            sys.exit(1)
        suites.append((label, path))

    print(f"{Colors.BLUE}{Colors.BOLD}============================================================")
    print("             STARTING CATCH2 UNIT VALIDATION MATRIX         ")
    print(f"============================================================{Colors.END}\n")

    results = []
    failed_count = 0
    total_count = 0

    for label, bin_path in suites:
        print(f"{Colors.BOLD}--- Running {label} Catch2 Suite ({bin_path}) ---{Colors.END}")
        for backend in backends:
            print(f"Running {label:<7} on {Colors.BLUE}{backend:<10}{Colors.END} ... ", end='', flush=True)
            total_count += 1
            env_override = {
                'GLIDE_WRAPPER_BACKEND': backend,
                'GLIDE_WRAPPER_MAX_FPS': '0',
            }
            if not args.windowed:
                env_override['GLIDE_FORCE_NO_WINDOW'] = '1'
            success, stdout, stderr, elapsed = run_command(
                [bin_path],
                env_override=env_override,
                env_remove=['GLIDE_BACKEND'],
            )
            status_str = f"{Colors.GREEN}[PASS]{Colors.END}" if success else f"{Colors.RED}[FAIL]{Colors.END}"
            print(f"{status_str} ({elapsed:.2f}s)")

            results.append({
                'backend': backend,
                'type': label,
                'name': os.path.basename(bin_path),
                'success': success,
                'elapsed': elapsed,
                'details': '' if success else (stdout + "\n" + stderr),
            })
            if not success:
                failed_count += 1
        print()

    print(f"{Colors.BOLD}============================================================")
    print("                 CONSOLIDATED TEST MATRIX SUMMARY            ")
    print(f"============================================================{Colors.END}")
    print(f" {'Backend':<12} | {'Suite':<8} | {'Binary':<20} | {'Status':<8} | {'Time':<6}")
    print("-" * 68)

    for r in results:
        color_status = f"{Colors.GREEN}PASS{Colors.END}" if r['success'] else f"{Colors.RED}FAIL{Colors.END}"
        time_str = f"{r['elapsed']:.2f}s"
        print(f" {r['backend']:<12} | {r['type']:<8} | {r['name']:<20} | {color_status:<4} | {time_str:<6}")

    print("-" * 68)

    if failed_count == 0:
        print(f"\n{Colors.GREEN}{Colors.BOLD}All {total_count}/{total_count} configurations passed!{Colors.END}")
        sys.exit(0)
    else:
        print(f"\n{Colors.RED}{Colors.BOLD}{failed_count}/{total_count} configurations failed.{Colors.END}")
        print(f"\n{Colors.YELLOW}--- Detailing Failures ---{Colors.END}")
        for r in results:
            if not r['success']:
                print(f"\n{Colors.RED}{Colors.BOLD}[FAILED] {r['backend']} | {r['type']} | {r['name']}{Colors.END}")
                print(r['details'][-2500:])
        sys.exit(1)


if __name__ == '__main__':
    main()
