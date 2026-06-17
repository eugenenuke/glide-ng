#!/usr/bin/env python3
import os
import subprocess
import sys
import time

# Color formatting helper
class Colors:
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    BOLD = '\033[1m'
    END = '\033[0m'

def run_command(cmd, env_override=None):
    env = os.environ.copy()
    if env_override:
        env.update(env_override)
    
    start_time = time.time()
    process = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        shell=True
    )
    stdout, stderr = process.communicate()
    elapsed = time.time() - start_time
    
    return process.returncode == 0, stdout.decode('utf-8', errors='replace'), stderr.decode('utf-8', errors='replace'), elapsed

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="Glide Wrapper Test Matrix Runner")
    parser.add_argument('--smoke', '-s', action='store_true', help="Run in fast smoke test mode (subset of tests)")
    parser.add_argument('--backend', '-b', choices=['software', 'vulkan', 'opengl_es'], help="Filter by a specific backend")
    parser.add_argument('--tests', '-t', help="Comma-separated list of visual tests to run (e.g., test00,test14)")
    parser.add_argument('--skip-unit', action='store_true', help="Skip running Catch2 unit tests")
    
    args = parser.parse_args()
    
    backends = [args.backend] if args.backend else ['software', 'vulkan', 'opengl_es']
    smoke_mode = args.smoke
    
    if args.tests:
        visual_tests = [t.strip() for t in args.tests.split(',') if t.strip()]
    elif smoke_mode:
        visual_tests = ['test00', 'test12', 'test14', 'test21']
    else:
        visual_tests = ['test00', 'test01', 'test02', 'test03', 'test04', 'test05', 'test06', 'test07', 'test08', 'test09', 'test10', 'test11', 'test13', 'test14', 'test15', 'test17', 'test18', 'test19', 'test21', 'test26', 'test29']
    
    catch2_bin = './build/bin/GlideWrapperTests'
    if not os.path.exists(catch2_bin):
        # Try build directory prefix
        catch2_bin = 'build/bin/GlideWrapperTests'
        if not os.path.exists(catch2_bin):
            print(f"{Colors.RED}{Colors.BOLD}[ERROR]{Colors.END} Catch2 binary not found! Please build the project first.")
            sys.exit(1)

    print(f"{Colors.BLUE}{Colors.BOLD}============================================================")
    if smoke_mode:
        print("             STARTING FAST SMOKE VALIDATION MATRIX          ")
    else:
        print("             STARTING ULTIMATE VALIDATION MATRIX            ")
    print(f"============================================================{Colors.END}\n")

    results = []
    failed_count = 0
    total_count = 0

    # 1. Run Catch2 Unit Tests across all backends
    if not args.skip_unit:
        print(f"{Colors.BOLD}--- [1/2] Running Catch2 Architectural Validation Suite ---{Colors.END}")
        for backend in backends:
            print(f"Running Catch2 on {Colors.BLUE}{backend:<10}{Colors.END} ... ", end='', flush=True)
            total_count += 1
            success, stdout, stderr, elapsed = run_command(catch2_bin, {'GLIDE_BACKEND': backend})
            
            status_str = f"{Colors.GREEN}[PASS]{Colors.END}" if success else f"{Colors.RED}[FAIL]{Colors.END}"
            print(f"{status_str} ({elapsed:.2f}s)")
            
            results.append({
                'backend': backend,
                'type': 'Catch2 Unit',
                'name': 'All Unit',
                'success': success,
                'elapsed': elapsed,
                'details': '' if success else (stdout + "\n" + stderr)
            })
            if not success:
                failed_count += 1
        print()
    else:
        print(f"{Colors.YELLOW}--- [1/2] Skipping Catch2 Architectural Validation Suite ---{Colors.END}\n")

    # 2. Run Visual Regression Tests across all backends
    print(f"{Colors.BOLD}--- [2/2] Running Glide Visual Regression Suite (test00-07) ---{Colors.END}")
    for backend in backends:
        for t in visual_tests:
            print(f"Running {t:<6} on {Colors.BLUE}{backend:<10}{Colors.END} ... ", end='', flush=True)
            total_count += 1
            cmd = f"python3 tests/run_tests.py {t}"
            success, stdout, stderr, elapsed = run_command(cmd, {'GLIDE_BACKEND': backend})
            
            status_str = f"{Colors.GREEN}[PASS]{Colors.END}" if success else f"{Colors.RED}[FAIL]{Colors.END}"
            print(f"{status_str} ({elapsed:.2f}s)")
            
            results.append({
                'backend': backend,
                'type': 'Visual Regr',
                'name': t,
                'success': success,
                'elapsed': elapsed,
                'details': '' if success else (stdout + "\n" + stderr)
            })
            if not success:
                failed_count += 1

    # Print Consolidated Summary Table
    print("\n")
    print(f"{Colors.BOLD}============================================================")
    print("                 CONSOLIDATED TEST MATRIX SUMMARY            ")
    print(f"============================================================{Colors.END}")
    print(f" {'Backend':<12} | {'Test Type':<12} | {'Target':<10} | {'Status':<8} | {'Time':<6}")
    print("-" * 60)
    
    for r in results:
        backend_str = f"{Colors.BLUE}{r['backend']:<12}{Colors.END}"
        type_str = f"{r['type']:<12}"
        name_str = f"{r['name']:<10}"
        status_str = f"{Colors.GREEN}PASS{Colors.END}" if r['success'] else f"{Colors.RED}FAIL{Colors.END}"
        status_padded = f"{status_str:<8}" # Color codes break standard padding, but we do our best
        time_str = f"{r['elapsed']:.2f}s"
        
        # Adjusting status string padding manually due to ANSI escapes
        color_status = f"{Colors.GREEN}PASS{Colors.END}" if r['success'] else f"{Colors.RED}FAIL{Colors.END}"
        print(f" {r['backend']:<12} | {r['type']:<12} | {r['name']:<10} | {color_status:<4} | {time_str:<6}")

    print("-" * 60)
    
    # Final verdict
    if failed_count == 0:
        print(f"\n{Colors.GREEN}{Colors.BOLD}🎉 EXCELLENT! All {total_count}/{total_count} configurations passed perfectly!{Colors.END}")
        sys.exit(0)
    else:
        print(f"\n{Colors.RED}{Colors.BOLD}❌ FAILURE! {failed_count}/{total_count} configurations failed.{Colors.END}")
        print(f"\n{Colors.YELLOW}--- Detailing Failures ---{Colors.END}")
        for r in results:
            if not r['success']:
                print(f"\n{Colors.RED}{Colors.BOLD}[FAILED] {r['backend']} | {r['type']} | {r['name']}{Colors.END}")
                print(r['details'])
        sys.exit(1)

if __name__ == '__main__':
    main()
