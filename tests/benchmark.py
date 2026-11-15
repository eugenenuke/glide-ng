import subprocess
import time
import sys
import os

def run_benchmark(test_name, frames=120):
    cmd = f"./{test_name} -n {frames}"
    env = {
        "LD_LIBRARY_PATH": "../../build/lib",
        "GLIDE_NG_HEADLESS": "1",
        "GLIDE_NG_BACKEND": os.environ.get("GLIDE_NG_BACKEND", "vulkan"),
        "GLIDE_NG_MAX_FPS": "0",
        "GLIDE_NG_SW_AA": os.environ.get("GLIDE_NG_SW_AA", "analytical")
    }
    
    print(f"Running benchmark: {test_name} for {frames} frames...")
    
    # Warmup run to compile pipelines and load resources
    subprocess.run(cmd, shell=True, env=env, cwd="ext_tests/build_tests", stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    
    # Active timing run
    start = time.perf_counter()
    res = subprocess.run(cmd, shell=True, env=env, cwd="ext_tests/build_tests", stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    end = time.perf_counter()
    
    if res.returncode != 0:
        print(f"Error running benchmark {test_name}! Return code: {res.returncode}")
        print("Stderr:", res.stderr.decode('utf-8'))
        return None
        
    elapsed = end - start
    fps = frames / elapsed
    frame_time_ms = (elapsed / frames) * 1000
    
    print(f"Completed in {elapsed:.4f}s | FPS: {fps:.2f} | Frame Time: {frame_time_ms:.2f} ms\n")
    return fps, frame_time_ms

if __name__ == "__main__":
    backend = os.environ.get("GLIDE_NG_BACKEND", "vulkan")
    aa_mode = os.environ.get("GLIDE_NG_SW_AA", "analytical")
    print("====================================================")
    print(f"    GLIDE BENCHMARK: {backend.upper()} ({aa_mode.upper()}) ")
    print("====================================================")
    
    results = {}
    for test in ["test14", "test21"]:
        res = run_benchmark(test)
        if res:
            results[test] = res
            
    print("====================================================")
    print("                 BENCHMARK RESULTS                  ")
    print("====================================================")
    for test, (fps, ms) in results.items():
        print(f"{test:<8}: {fps:>6.2f} FPS ({ms:>5.2f} ms/frame)")
    print("====================================================")
