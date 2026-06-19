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
    
    parser = argparse.ArgumentParser(description="Glide Wrapper Catch2 Unit Test Suite Runner")
    parser.add_argument('--backend', '-b', choices=['software', 'sw', 'vulkan', 'vk', 'opengl_es', 'gles', 'goes'], help="Filter by a specific backend")
    
    args = parser.parse_args()
    
    # Map backend shortcuts to canonical names
    backend_map = {
        'software': 'software', 'sw': 'software',
        'vulkan': 'vulkan', 'vk': 'vulkan',
        'opengl_es': 'opengl_es', 'gles': 'opengl_es', 'goes': 'opengl_es'
    }
    
    if args.backend:
        backends = [backend_map[args.backend]]
    else:
        backends = ['software', 'vulkan', 'opengl_es']
    
    catch2_bin = './build/bin/GlideWrapperTests'
    if not os.path.exists(catch2_bin):
        # Try build directory prefix
        catch2_bin = 'build/bin/GlideWrapperTests'
        if not os.path.exists(catch2_bin):
            print(f"{Colors.RED}{Colors.BOLD}[ERROR]{Colors.END} Catch2 binary not found! Please build the project first.")
            sys.exit(1)

    print(f"{Colors.BLUE}{Colors.BOLD}============================================================")
    print("             STARTING CATCH2 UNIT VALIDATION MATRIX         ")
    print(f"============================================================{Colors.END}\n")

    results = []
    failed_count = 0
    total_count = 0

    print(f"{Colors.BOLD}--- Running Catch2 Architectural Validation Suite ---{Colors.END}")
    for backend in backends:
        print(f"Running Catch2 on {Colors.BLUE}{backend:<10}{Colors.END} ... ", end='', flush=True)
        total_count += 1
        success, stdout, stderr, elapsed = run_command(catch2_bin, {
            'GLIDE_BACKEND': backend,
            'GLIDE_WRAPPER_MAX_FPS': '0'
        })
        
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

    # Print Consolidated Summary Table
    print(f"{Colors.BOLD}============================================================")
    print("                 CONSOLIDATED TEST MATRIX SUMMARY            ")
    print(f"============================================================{Colors.END}")
    print(f" {'Backend':<12} | {'Test Type':<12} | {'Target':<10} | {'Status':<8} | {'Time':<6}")
    print("-" * 60)
    
    for r in results:
        color_status = f"{Colors.GREEN}PASS{Colors.END}" if r['success'] else f"{Colors.RED}FAIL{Colors.END}"
        time_str = f"{r['elapsed']:.2f}s"
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
