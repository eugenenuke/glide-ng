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
    "test04": 0.95,  # Software triangle rasterizer vs Mesa GL native triangle rounding and boundary alignment variance
    "test14": 0.98,  # Minor diagonal intersection boundary shift under hardware rendering precision differences
}

# Custom console text block skip bounds: (max_bx, max_by)
# Defaults to (15, 2) which covers x < 240, y < 32
TEST_TEXT_SKIP_BOUNDS = {
    "test10": (40, 2),  # Widen to full screen width for the top 2 rows to cover all console text/headers
    "test13": (40, 2),  # Widen to full screen width for the top 2 rows to cover all console text/headers
    "test14": (40, 2),  # Widen to full screen width for the top 2 rows to cover all console text/headers
    "test15": (40, 2),  # Widen to full screen width for the top 2 rows to cover all console text/headers
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

def count_text_pixels(pixels, width, height):
    count = 0
    # Search the top-left console text area (x < 300, y < 32)
    for y in range(0, 32):
        for x in range(0, 300):
            if x >= width or y >= height: break
            idx = (y * width + x) * 3
            r, g, b = pixels[idx], pixels[idx+1], pixels[idx+2]
            # White/gray text detector: high brightness, low saturation
            if r > 100 and g > 100 and b > 100 and abs(int(r)-int(g)) < 40 and abs(int(g)-int(b)) < 40:
                count += 1
    return count

def count_geometry_pixels(pixels, width, height, threshold=8):
    # Sample background color from bottom-right corner to be clear-color independent!
    bg_idx = ((height - 5) * width + (width - 5)) * 3
    bg_r, bg_g, bg_b = pixels[bg_idx], pixels[bg_idx+1], pixels[bg_idx+2]
    
    count = 0
    # Scan the active geometry region (below y=48 console text boundary)
    for y in range(48, height):
        for x in range(0, width):
            idx = (y * width + x) * 3
            r, g, b = pixels[idx], pixels[idx+1], pixels[idx+2]
            if max(abs(int(r)-int(bg_r)), abs(int(g)-int(bg_g)), abs(int(b)-int(bg_b))) > threshold:
                count += 1
    return count

def compare_images(ref_path, test_path, test_name, threshold=8, pass_ratio=None, block_size=16, block_pass_ratio=0.85):
    if not os.path.exists(ref_path):
        return False, f"Reference image missing: {ref_path}"
    if not os.path.exists(test_path):
        return False, f"Test output image missing: {test_path}"

    ref_pixels = load_ppm(ref_path)
    test_pixels = load_ppm(test_path)

    if len(ref_pixels) != len(test_pixels):
        return False, f"Resolution mismatch: Ref len={len(ref_pixels)}, Test len={len(test_pixels)}"

    total_bytes = len(ref_pixels)
    if total_bytes == 640 * 480 * 3:
        width, height = 640, 480
    elif total_bytes == 800 * 600 * 3:
        width, height = 800, 600
    else:
        width, height = None, None

    if width is None or height is None:
        # Fallback to global comparison if resolution is non-standard
        matching_pixels = 0
        total_pixels = total_bytes // 3
        for i in range(0, total_bytes, 3):
            r_ref, g_ref, b_ref = ref_pixels[i], ref_pixels[i+1], ref_pixels[i+2]
            r_test, g_test, b_test = test_pixels[i], test_pixels[i+1], test_pixels[i+2]
            if max(abs(int(r_ref)-int(r_test)), abs(int(g_ref)-int(g_test)), abs(int(b_ref)-int(b_test))) <= threshold:
                matching_pixels += 1
        ratio = matching_pixels / total_pixels
        passed = ratio >= pass_ratio
        return passed, f"Global Parity: {ratio*100:.2f}% (Matching: {matching_pixels}/{total_pixels})"

    # Explicit Console Text Presence Check
    ref_text_pixels = count_text_pixels(ref_pixels, width, height)
    test_text_pixels = count_text_pixels(test_pixels, width, height)
    
    # If reference has text, test MUST have text too, and it must NOT be corrupted into solid blocks!
    if ref_text_pixels > 150:
        if test_text_pixels < 50: # Text is completely missing!
            return False, f"FAILED: Console text is missing! (Ref has {ref_text_pixels} text pixels, Test has only {test_text_pixels})"
        if test_text_pixels > 4 * ref_text_pixels: # Text is corrupted (e.g. solid untextured blocks!)
            return False, f"FAILED: Console text is corrupted/drawn as solid blocks! (Ref has {ref_text_pixels} text pixels, Test has {test_text_pixels} - expected text to not exceed {4 * ref_text_pixels})"

    # Explicit Geometry Presence Check (closes the loophole for tiny primitives!)
    ref_geom_pixels = count_geometry_pixels(ref_pixels, width, height, threshold)
    test_geom_pixels = count_geometry_pixels(test_pixels, width, height, threshold)
    
    # If reference expects rendering in the geometry region, test must render it!
    if ref_geom_pixels > 30:
        if test_geom_pixels < 5: # Geometry is completely missing!
            return False, f"FAILED: Rendered geometry is missing! (Ref has {ref_geom_pixels} geometry pixels, Test has only {test_geom_pixels})"

    # Block-based comparison for the rest of the screen
    blocks_x = width // block_size
    blocks_y = height // block_size
    failed_blocks = 0
    total_blocks = blocks_x * blocks_y

    def get_pixel(pixels, x, y):
        idx = (y * width + x) * 3
        return pixels[idx], pixels[idx+1], pixels[idx+2]

    mismatched_block_details = []

    max_bx, max_by = TEST_TEXT_SKIP_BOUNDS.get(test_name, (15, 2))
    for by in range(blocks_y):
        for bx in range(blocks_x):
            # Skip the console text blocks because we already verified text presence above!
            # This avoids pixel-level font rendering and anti-aliasing noise.
            if by < max_by and bx < max_bx:
                continue

            block_total_pixels = block_size * block_size
            shift = 2  # Allow up to 2 pixels of translation alignment
            
            # Check 1: Test -> Reference
            test_to_ref_matches = 0
            for y_offset in range(block_size):
                y = by * block_size + y_offset
                for x_offset in range(block_size):
                    x = bx * block_size + x_offset
                    
                    r_test, g_test, b_test = get_pixel(test_pixels, x, y)
                    
                    # Direct Match Check First (Performance Optimization)
                    r_ref_dir, g_ref_dir, b_ref_dir = get_pixel(ref_pixels, x, y)
                    if max(abs(int(r_ref_dir)-int(r_test)), abs(int(g_ref_dir)-int(g_test)), abs(int(b_ref_dir)-int(b_test))) <= threshold:
                        test_to_ref_matches += 1
                        continue
                        
                    # Neighborhood Search on Mismatch
                    matched = False
                    for ny in range(max(0, y - shift), min(height, y + shift + 1)):
                        for nx in range(max(0, x - shift), min(width, x + shift + 1)):
                            r_ref, g_ref, b_ref = get_pixel(ref_pixels, nx, ny)
                            if max(abs(int(r_ref)-int(r_test)), abs(int(g_ref)-int(g_test)), abs(int(b_ref)-int(b_test))) <= threshold:
                                matched = True
                                break
                        if matched:
                            break
                    if matched:
                        test_to_ref_matches += 1

            # Check 2: Reference -> Test
            ref_to_test_matches = 0
            for y_offset in range(block_size):
                y = by * block_size + y_offset
                for x_offset in range(block_size):
                    x = bx * block_size + x_offset
                    
                    r_ref, g_ref, b_ref = get_pixel(ref_pixels, x, y)
                    
                    # Direct Match Check First (Performance Optimization)
                    r_test_dir, g_test_dir, b_test_dir = get_pixel(test_pixels, x, y)
                    if max(abs(int(r_ref)-int(r_test_dir)), abs(int(g_ref)-int(g_test_dir)), abs(int(b_ref)-int(b_test_dir))) <= threshold:
                        ref_to_test_matches += 1
                        continue
                        
                    # Neighborhood Search on Mismatch
                    matched = False
                    for ny in range(max(0, y - shift), min(height, y + shift + 1)):
                        for nx in range(max(0, x - shift), min(width, x + shift + 1)):
                            r_test, g_test, b_test = get_pixel(test_pixels, nx, ny)
                            if max(abs(int(r_ref)-int(r_test)), abs(int(g_ref)-int(g_test)), abs(int(b_ref)-int(b_test))) <= threshold:
                                matched = True
                                break
                        if matched:
                            break
                    if matched:
                        ref_to_test_matches += 1

            block_matching_pixels = min(test_to_ref_matches, ref_to_test_matches)
            block_ratio = block_matching_pixels / block_total_pixels
            if block_ratio < block_pass_ratio:
                failed_blocks += 1
                mismatched_block_details.append(f"Block({bx},{by}): {block_ratio*100:.1f}%")

    allowed_failed_blocks = int((1.0 - pass_ratio) * total_blocks) if pass_ratio is not None else 0
    passed = failed_blocks <= allowed_failed_blocks
    if passed:
        matching_pixels = 0
        total_pixels = width * height
        for i in range(0, total_bytes, 3):
            r_ref, g_ref, b_ref = ref_pixels[i], ref_pixels[i+1], ref_pixels[i+2]
            r_test, g_test, b_test = test_pixels[i], test_pixels[i+1], test_pixels[i+2]
            if max(abs(int(r_ref)-int(r_test)), abs(int(g_ref)-int(g_test)), abs(int(b_ref)-int(b_test))) <= threshold:
                matching_pixels += 1
        global_ratio = matching_pixels / total_pixels
        msg = f"Passed ({failed_blocks}/{total_blocks} blocks failed. Allowed: {allowed_failed_blocks}. Global Parity: {global_ratio*100:.2f}%)"
    else:
        msg = f"Failed! {failed_blocks}/{total_blocks} blocks mismatched. Allowed: {allowed_failed_blocks}. Details: " + ", ".join(mismatched_block_details[:3])
        if len(mismatched_block_details) > 3:
            msg += f" ... and {len(mismatched_block_details)-3} more blocks."
            
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
        
        pass_ratio = TEST_PASS_RATIOS.get(test_name, None)
        passed, msg = compare_images(ref_path_full, test_path_full, test_name, pass_ratio=pass_ratio)
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
