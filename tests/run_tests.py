#!/usr/bin/env python3
import os
import subprocess
import sys

# Color constants for nice CLI output
GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
RESET = "\033[0m"

# Paths
WORKSPACE = "/usr/local/google/home/eianiuk/gemini/3dfx"
TEST_RUNNER = "./build/bin/test_runner"
REF_DIR = "tests/reference_dumps"

# Dynamically resolve test dumps directory based on active backend
backend = os.environ.get("GLIDE_BACKEND", "vulkan").lower()
if backend == "opengl_es":
    TEST_DIR = "tests/opengl_es_dumps"
elif backend == "software":
    TEST_DIR = "tests/software_dumps"
else:
    TEST_DIR = "tests/vulkan_dumps"

OPENGLIDE_LIB_DIR = "ext_tests/build_tests/libs/"
OUR_LIB_DIR = "build/lib/"
SDL2_PRELOAD = "/usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0"

# Per-test visual regression pass ratio overrides (defaults to 0.98 if unspecified)
TEST_PASS_RATIOS = {
    "test02": 0.95,  # Bresenham integer lines vs Mesa software GL native line rasterization rounding variance
}

def ensure_dirs():
    os.makedirs(os.path.join(WORKSPACE, REF_DIR), exist_ok=True)
    os.makedirs(os.path.join(WORKSPACE, TEST_DIR), exist_ok=True)

def run_command_with_env(cmd, library_path, force_no_window=False, use_xvfb=False):
    env = os.environ.copy()
    env["LD_LIBRARY_PATH"] = os.path.join(WORKSPACE, library_path)
    
    if force_no_window:
        env["GLIDE_FORCE_NO_WINDOW"] = "1"
    else:
        # Preload SDL2 for openglide
        env["LD_PRELOAD"] = SDL2_PRELOAD

    if use_xvfb:
        full_cmd = ["xvfb-run", "-a", "-s", "-screen 0 640x480x24"] + cmd
    else:
        full_cmd = cmd

    result = subprocess.run(full_cmd, cwd=WORKSPACE, capture_output=True, text=True, env=env)
    return result

def load_ppm(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()
    
    # Simple PPM header parsing: find start of pixels after 3 newlines
    header_count = 0
    idx = 0
    while header_count < 3 and idx < len(data):
        if data[idx] == ord('\n'):
            header_count += 1
        idx += 1
    return data[idx:]

def compare_images(ref_path, test_path, threshold=8, pass_ratio=0.98):
    if not os.path.exists(ref_path):
        return False, f"Reference image missing: {ref_path}"
    if not os.path.exists(test_path):
        return False, f"Test output image missing: {test_path}"

    ref_pixels = load_ppm(ref_path)
    test_pixels = load_ppm(test_path)

    if len(ref_pixels) != len(test_pixels):
        return False, f"Resolution mismatch: Ref len={len(ref_pixels)}, Test len={len(test_pixels)}"

    matching_pixels = 0
    total_pixels = len(ref_pixels) // 3

    for i in range(0, len(ref_pixels), 3):
        r_ref, g_ref, b_ref = ref_pixels[i], ref_pixels[i+1], ref_pixels[i+2]
        r_test, g_test, b_test = test_pixels[i], test_pixels[i+1], test_pixels[i+2]

        diff_r = abs(int(r_ref) - int(r_test))
        diff_g = abs(int(g_ref) - int(g_test))
        diff_b = abs(int(b_ref) - int(b_test))

        if max(diff_r, diff_g, diff_b) <= threshold:
            matching_pixels += 1

    ratio = matching_pixels / total_pixels
    passed = ratio >= pass_ratio
    msg = f"Parity: {ratio*100:.2f}% (Matching: {matching_pixels}/{total_pixels}, threshold={threshold})"
    return passed, msg

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <test_name>")
        sys.exit(1)
        
    test_name = sys.argv[1]
    ensure_dirs()

    # Find existing reference files
    ref_files = [f for f in os.listdir(os.path.join(WORKSPACE, REF_DIR)) if f.startswith(f"{test_name}_mode")]
    
    if not ref_files:
        print(f"{YELLOW}No reference images for {test_name} found. Generating using OpenGLide...{RESET}")
        res = run_command_with_env([TEST_RUNNER, "--test", test_name, "--mode", "capture"], 
                                   OPENGLIDE_LIB_DIR, force_no_window=False, use_xvfb=True)
        if res.returncode != 0:
            print(f"{RED}Failed to generate reference images!{RESET}")
            print(res.stderr)
            sys.exit(1)
        ref_files = [f for f in os.listdir(os.path.join(WORKSPACE, REF_DIR)) if f.startswith(f"{test_name}_mode")]
        print(f"{GREEN}Successfully generated {len(ref_files)} reference image(s).{RESET}")

    # Run our library
    print(f"Running {test_name} using our library (headless)...")
    res = run_command_with_env([TEST_RUNNER, "--test", test_name, "--mode", "run"], 
                               OUR_LIB_DIR, force_no_window=True, use_xvfb=False)
    if res.returncode != 0:
        print(f"{RED}Test failed to run or crashed under our library!{RESET}")
        print(res.stderr)
        sys.exit(1)

    # Compare all files
    all_passed = True
    for f in sorted(ref_files):
        ref_path_full = os.path.join(WORKSPACE, REF_DIR, f)
        test_path_full = os.path.join(WORKSPACE, TEST_DIR, f)
        
        pass_ratio = TEST_PASS_RATIOS.get(test_name, 0.98)
        passed, msg = compare_images(ref_path_full, test_path_full, pass_ratio=pass_ratio)
        if passed:
            print(f"{GREEN}[PASS] {f} matches! {msg}{RESET}")
        else:
            print(f"{RED}[FAIL] {f} mismatch! {msg}{RESET}")
            all_passed = False
            
    if not all_passed:
        print(f"{RED}Visual regression check failed for {test_name}!{RESET}")
        sys.exit(1)
    else:
        print(f"{GREEN}All modes passed visual regression check for {test_name}!{RESET}")

if __name__ == "__main__":
    main()
