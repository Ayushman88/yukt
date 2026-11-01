#!/usr/bin/env python3
"""Time YUKT / C++ / Rust kernel compute, not process startup."""

from __future__ import annotations

import csv
import json
import math
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BENCH = Path(__file__).resolve().parent
OUT = BENCH / "out"
KERNELS = ["nested", "matmul", "fibonacci", "sort", "alloc", "parallel"]
RUNS = 15
MAIN_DEFINE = re.compile(r"(define[^\n]*?)@main\(")
COMPUTE_NS = re.compile(r"compute_ns=(\d+)")

SIZES = {
    "nested": "n=8000 nested a[j]+=i plus checksum",
    "matmul": "512x512 integer matmul, naive ijk",
    "fibonacci": "1_500_000 calls of iterative fib(45) (opaque arg)",
    "sort": "selection sort, n=16000",
    "alloc": "zeros + 25 fills, n=4_000_000",
    "parallel": "independent fill + sequential reduction, n=20_000_000",
}


def run(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True, check=False)


def first_line(command: str) -> str:
    result = run(["/bin/sh", "-c", command])
    return result.stdout.strip().splitlines()[0] if result.stdout.strip() else ""


def macos_sysroot() -> str:
    if sys.platform != "darwin":
        return ""
    return first_line("xcrun --show-sdk-path 2>/dev/null")


def find_clang() -> str:
    env = os.environ.get("YUKT_CLANG")
    if env:
        return env
    brew_llvm = Path("/opt/homebrew/opt/llvm/bin/clang")
    if brew_llvm.exists():
        return str(brew_llvm)
    local_llvm = Path("/usr/local/opt/llvm/bin/clang")
    if local_llvm.exists():
        return str(local_llvm)
    bindir = first_line("llvm-config --bindir 2>/dev/null")
    if bindir:
        candidate = Path(bindir) / "clang"
        if candidate.exists():
            return str(candidate)
    if sys.platform == "darwin":
        path = first_line("xcrun -f clang 2>/dev/null")
        if path:
            return path
    return shutil.which("clang") or "clang"


def find_cxx() -> str:
    env = os.environ.get("CXX")
    if env:
        return env
    sibling = Path(find_clang()).parent / "clang++"
    if sibling.exists():
        return str(sibling)
    brew_llvm = Path("/opt/homebrew/opt/llvm/bin/clang++")
    if brew_llvm.exists():
        return str(brew_llvm)
    local_llvm = Path("/usr/local/opt/llvm/bin/clang++")
    if local_llvm.exists():
        return str(local_llvm)
    return shutil.which("clang++") or shutil.which("c++") or "c++"


def host_cpu() -> str:
    if sys.platform == "darwin":
        brand = first_line("sysctl -n machdep.cpu.brand_string 2>/dev/null")
        if brand:
            return brand
    if sys.platform == "linux":
        try:
            for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
                if line.lower().startswith("model name"):
                    return line.split(":", 1)[1].strip()
        except OSError:
            pass
    return platform.processor() or platform.machine()


def fmt_secs(ns: float) -> str:
    return f"{ns / 1_000_000_000:.4f}s"


def parse_compute_ns(stderr: str) -> int | None:
    matches = COMPUTE_NS.findall(stderr)
    if not matches:
        return None
    return int(matches[-1])


def measure(cmd: list[str]) -> tuple[list[int], str]:
    samples: list[int] = []
    stdout = ""
    for _ in range(RUNS):
        result = run(cmd)
        if result.returncode != 0:
            raise RuntimeError(f"{' '.join(cmd)} failed:\n{result.stdout}\n{result.stderr}")
        ns = parse_compute_ns(result.stderr)
        if ns is None:
            raise RuntimeError(f"{' '.join(cmd)} did not print compute_ns on stderr:\n{result.stderr}")
        samples.append(ns)
        stdout = result.stdout
    return samples, stdout


def percentile(ordered: list[int], p: float) -> float:
    if not ordered:
        return 0.0
    rank = (len(ordered) - 1) * p
    low = math.floor(rank)
    high = math.ceil(rank)
    if low == high:
        return float(ordered[low])
    return float(ordered[low]) + (ordered[high] - ordered[low]) * (rank - low)


def summarize(samples: list[int]) -> dict[str, float]:
    ordered = sorted(samples)
    median = float(statistics.median(ordered))
    mad = float(statistics.median([abs(sample - median) for sample in ordered]))
    return {
        "median": median,
        "min": float(ordered[0]),
        "max": float(ordered[-1]),
        "q1": percentile(ordered, 0.25),
        "q3": percentile(ordered, 0.75),
        "iqr": percentile(ordered, 0.75) - percentile(ordered, 0.25),
        "mad": mad,
    }


def strip_crt_main(llvm_ir: str) -> str:
    stripped, count = MAIN_DEFINE.subn(r"\1@yukt_unused_crt_main(", llvm_ir, count=1)
    if count != 1:
        raise RuntimeError("could not rename LLVM @main in YUKT IR")
    return stripped


def find_llc(clang: str) -> str:
    sibling = Path(clang).parent / "llc"
    if sibling.exists():
        return str(sibling)
    return shutil.which("llc") or "llc"


def compile_yukt_kernel(yukt: Path, clang: str, name: str, runtime: Path) -> list[str]:
    src = BENCH / "yukt" / f"{name}.yukt"
    ll_path = OUT / f"yukt_{name}.ll"
    bin_path = OUT / f"yukt_{name}"
    result = run([str(yukt), "--emit-llvm", str(src)])
    if result.returncode != 0:
        raise RuntimeError(f"yukt --emit-llvm {src} failed:\n{result.stdout}\n{result.stderr}")
    ll_path.write_text(strip_crt_main(result.stdout), encoding="utf-8")

    sysroot = macos_sysroot()
    llc = find_llc(clang)
    kernel_obj = OUT / f"yukt_{name}.o"
    driver_obj = OUT / "driver.o"
    ir_cmd = [llc, "-O3", "-relocation-model=pic", "-filetype=obj", str(ll_path), "-o", str(kernel_obj)]
    driver_cmd = [clang, "-c", "-O3", str(BENCH / "driver.c"), "-o", str(driver_obj)]
    link_cmd = [clang, "-O3", "-o", str(bin_path), str(driver_obj), str(kernel_obj), str(runtime)]
    if sysroot:
        for cmd in (driver_cmd, link_cmd):
            cmd.extend(["-isysroot", sysroot])
    if sys.platform != "darwin":
        link_cmd.append("-pthread")
    link_cmd.append("-lm")
    for cmd in (ir_cmd, driver_cmd, link_cmd):
        built = run(cmd)
        if built.returncode != 0:
            raise RuntimeError(f"link YUKT {name} failed:\n{' '.join(cmd)}\n{built.stdout}\n{built.stderr}")
    return [str(bin_path)]


def compile_time(cmd: list[str]) -> float:
    started = time.perf_counter()
    result = run(cmd)
    elapsed = time.perf_counter() - started
    if result.returncode != 0:
        raise RuntimeError(f"compile failed: {' '.join(cmd)}\n{result.stdout}\n{result.stderr}")
    return elapsed


def winner_line(
    name: str,
    yukt: float,
    cpp: float,
    rust: float,
    cpp_par: float | None = None,
    rust_par: float | None = None,
) -> str:
    ranked = [("YUKT", yukt), ("C++ seq", cpp), ("Rust seq", rust)]
    if cpp_par is not None:
        ranked.append(("C++ par", cpp_par))
    if rust_par is not None:
        ranked.append(("Rust par", rust_par))
    ranked.sort(key=lambda item: item[1])
    best_name, best = ranked[0]
    labels = ", ".join(f"{label} {fmt_secs(value)}" for label, value in ranked)
    if best < 1_000_000:
        return f"{name}: no speed claim — a median was under 1ms ({labels}), so a compiler likely folded the kernel."
    second_name, second = ranked[1]
    if best * 1.05 >= second:
        return f"{name}: too close to call — {best_name} {fmt_secs(best)} vs {second_name} {fmt_secs(second)}. Full order: {labels}."
    return (
        f"{name}: {best_name} {fmt_secs(best)} vs {second_name} {fmt_secs(second)} "
        f"({second / best:.2f}×). Full order: {labels}."
    )


def write_report(
    rows: list[dict[str, object]],
    compile_rows: list[dict[str, object]],
    notes: list[str],
    claims: list[str],
) -> None:
    lines = [
        "# YUKT benchmarks",
        "",
        "These numbers are **in-process kernel time**, not process startup. Each binary "
        "discards one warmup call, then times the second call with a monotonic clock "
        f"(`compute_ns` on stderr). The table is the median of {RUNS} process runs.",
        "",
        "## Compute",
        "",
        "| Kernel | Size | YUKT | C++ seq | C++ par | Rust seq | Rust par | YUKT IQR | YUKT MAD | YUKT min–max | Checksum |",
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        cpp_par = "—" if row.get("cpp_par") is None else fmt_secs(row["cpp_par"])
        rust_par = "—" if row.get("rust_par") is None else fmt_secs(row["rust_par"])
        lines.append(
            f"| {row['name']} | {row['size']} | {fmt_secs(row['yukt'])} | {fmt_secs(row['cpp'])} | "
            f"{cpp_par} | {fmt_secs(row['rust'])} | {rust_par} | "
            f"{fmt_secs(row['yukt_iqr'])} | {fmt_secs(row['yukt_mad'])} | "
            f"{fmt_secs(row['yukt_min'])}–{fmt_secs(row['yukt_max'])} | {row['match']} |"
        )
    lines.extend(
        [
            "",
            "## Compile time and binary size (`matmul`)",
            "",
            "| Compiler | Compile time | Binary size |",
            "| --- | --- | --- |",
        ]
    )
    for row in compile_rows:
        lines.append(f"| {row['name']} | {row['time']:.4f}s | {row['size']} bytes |")
    lines.extend(["", "## What this run supports", ""])
    lines.extend(f"- {claim}" for claim in claims)
    lines.extend(["", "## Method", ""])
    lines.extend(f"- {note}" for note in notes)
    lines.append("")
    (ROOT / "docs" / "BENCHMARKS.md").write_text("\n".join(lines), encoding="utf-8")


BASELINE_0_08 = {
    "nested": 10_300_000,
    "matmul": 37_100_000,
    "fibonacci": 36_300_000,
    "sort": 38_400_000,
    "alloc": 24_500_000,
    "parallel": 9_600_000,
}

BASELINE_0_09 = {
    "nested": 10_337_000,
    "matmul": 49_455_000,
    "fibonacci": 36_292_000,
    "sort": 45_403_000,
    "alloc": 15_460_000,
    "parallel": 7_455_000,
}


def ns_record(samples: list[int] | None) -> dict[str, object] | None:
    if samples is None:
        return None
    summary = summarize(samples)
    return {
        "median_ns": int(summary["median"]),
        "min_ns": int(summary["min"]),
        "max_ns": int(summary["max"]),
        "q1_ns": int(summary["q1"]),
        "q3_ns": int(summary["q3"]),
        "iqr_ns": int(summary["iqr"]),
        "mad_ns": int(summary["mad"]),
        "spread_ns": int(summary["max"] - summary["min"]),
        "runs": samples,
    }


def write_machine_results(
    payload: dict[str, object],
    rows: list[dict[str, object]],
) -> None:
    out_dir = ROOT / "benchmarks"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "results.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    csv_path = out_dir / "results.csv"
    fieldnames = [
        "kernel",
        "size",
        "yukt_median_ns",
        "yukt_min_ns",
        "yukt_max_ns",
        "yukt_iqr_ns",
        "yukt_mad_ns",
        "cpp_seq_median_ns",
        "rust_seq_median_ns",
        "cpp_par_median_ns",
        "rust_par_median_ns",
        "baseline_0_08_ns",
        "baseline_0_09_ns",
        "checksum_match",
    ]
    with csv_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(
                {
                    "kernel": row["name"],
                    "size": row["size"],
                    "yukt_median_ns": int(row["yukt"]),
                    "yukt_min_ns": int(row["yukt_min"]),
                    "yukt_max_ns": int(row["yukt_max"]),
                    "yukt_iqr_ns": int(row["yukt_iqr"]),
                    "yukt_mad_ns": int(row["yukt_mad"]),
                    "cpp_seq_median_ns": int(row["cpp"]),
                    "rust_seq_median_ns": int(row["rust"]),
                    "cpp_par_median_ns": "" if row.get("cpp_par") is None else int(row["cpp_par"]),
                    "rust_par_median_ns": "" if row.get("rust_par") is None else int(row["rust_par"]),
                    "baseline_0_08_ns": BASELINE_0_08.get(row["name"], ""),
                    "baseline_0_09_ns": BASELINE_0_09.get(row["name"], ""),
                    "checksum_match": row["match"],
                }
            )


def main() -> int:
    yukt = Path(os.environ.get("YUKT", ROOT / "build" / "yukt"))
    runtime = Path(os.environ.get("YUKT_RUNTIME_LIB", ROOT / "build" / "libyukt_runtime.a"))
    if not yukt.exists():
        print(f"missing compiler: {yukt}", file=sys.stderr)
        return 1
    if not runtime.exists():
        print(f"missing runtime: {runtime}", file=sys.stderr)
        return 1

    cxx = find_cxx()
    rustc = shutil.which("rustc")
    clang = find_clang()
    if rustc is None:
        print("rustc is not on PATH", file=sys.stderr)
        return 1

    OUT.mkdir(parents=True, exist_ok=True)

    yukt_bins: dict[str, list[str]] = {}
    cpp_bins: dict[str, list[str]] = {}
    rust_bins: dict[str, list[str]] = {}
    compile_cmds: dict[str, tuple[list[str], list[str], list[str]]] = {}

    print("compiling kernels...")
    for name in KERNELS:
        cpp_bin = OUT / f"cpp_{name}"
        rust_bin = OUT / f"rust_{name}"
        cpp_src = BENCH / "cpp" / f"{name}.cpp"
        rust_src = BENCH / "rust" / f"{name}.rs"
        cpp_cmd = [
            cxx,
            "-O3",
            "-march=native",
            "-std=c++17",
            "-pthread",
            "-I",
            str(BENCH / "cpp"),
            str(cpp_src),
            "-o",
            str(cpp_bin),
        ]
        sysroot = macos_sysroot()
        if sysroot and "llvm" in cxx:
            cpp_cmd.extend(["-isysroot", sysroot])
        rust_cmd = [
            rustc,
            "-C",
            "opt-level=3",
            "-C",
            "target-cpu=native",
            str(rust_src),
            "-o",
            str(rust_bin),
        ]
        try:
            yukt_bins[name] = compile_yukt_kernel(yukt, clang, name, runtime)
        except RuntimeError as error:
            print(error, file=sys.stderr)
            return 1
        for cmd in (cpp_cmd, rust_cmd):
            result = run(cmd)
            if result.returncode != 0:
                print(result.stdout + result.stderr, file=sys.stderr)
                print("compile failed:", " ".join(cmd), file=sys.stderr)
                return 1
        cpp_bins[name] = [str(cpp_bin)]
        rust_bins[name] = [str(rust_bin)]
        compile_cmds[name] = (
            [str(yukt), "--emit-llvm", str(BENCH / "yukt" / f"{name}.yukt")],
            cpp_cmd,
            rust_cmd,
        )

    def compile_extra(label: str, cmd: list[str]) -> None:
        built = run(cmd)
        if built.returncode != 0:
            raise RuntimeError(f"compile {label} failed:\n{built.stdout}\n{built.stderr}")

    extra_bins: dict[str, list[str]] = {}
    sysroot = macos_sysroot()

    def cpp_extra(src: Path, dest: Path) -> list[str]:
        cmd = [
            cxx,
            "-O3",
            "-march=native",
            "-std=c++17",
            "-pthread",
            "-I",
            str(BENCH / "cpp"),
            str(src),
            "-o",
            str(dest),
        ]
        if sysroot and "llvm" in cxx:
            cmd.extend(["-isysroot", sysroot])
        compile_extra(src.name, cmd)
        return [str(dest)]

    def rust_extra(src: Path, dest: Path) -> list[str]:
        cmd = [rustc, "-C", "opt-level=3", "-C", "target-cpu=native", str(src), "-o", str(dest)]
        compile_extra(src.name, cmd)
        return [str(dest)]

    try:
        extra_bins["matmul_par_cpp"] = cpp_extra(BENCH / "cpp" / "matmul_par.cpp", OUT / "cpp_matmul_par")
        extra_bins["matmul_par_rust"] = rust_extra(BENCH / "rust" / "matmul_par.rs", OUT / "rust_matmul_par")
        extra_bins["parallel_seq_cpp"] = cpp_extra(BENCH / "cpp" / "parallel_seq.cpp", OUT / "cpp_parallel_seq")
        extra_bins["parallel_seq_rust"] = rust_extra(BENCH / "rust" / "parallel_seq.rs", OUT / "rust_parallel_seq")
        extra_bins["algo_sort_cpp"] = cpp_extra(BENCH / "algo" / "sort_std.cpp", OUT / "algo_sort_cpp")
        extra_bins["algo_sort_rust"] = rust_extra(BENCH / "algo" / "sort_std.rs", OUT / "algo_sort_rust")
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1

    rows = []
    claims = []
    yukt_wins = []
    for name in KERNELS:
        print(f"running {name}...")
        try:
            yukt_samples, yukt_out = measure(yukt_bins[name])
            cpp_samples, cpp_out = measure(cpp_bins[name])
            rust_samples, rust_out = measure(rust_bins[name])
        except RuntimeError as error:
            print(error, file=sys.stderr)
            return 1
        yukt_s = summarize(yukt_samples)
        cpp_s = summarize(cpp_samples)
        rust_s = summarize(rust_samples)
        cpp_par_samples = None
        rust_par_samples = None
        cpp_par_out = cpp_out
        rust_par_out = rust_out
        if name == "matmul":
            cpp_par_samples, cpp_par_out = measure(extra_bins["matmul_par_cpp"])
            rust_par_samples, rust_par_out = measure(extra_bins["matmul_par_rust"])
        elif name == "parallel":
            # Existing C++/Rust `parallel` kernels are explicit-thread fills.
            # sequential counterparts live in extra_bins; swap labels below.
            cpp_par_samples, cpp_par_out = cpp_samples, cpp_out
            rust_par_samples, rust_par_out = rust_samples, rust_out
            cpp_samples, cpp_out = measure(extra_bins["parallel_seq_cpp"])
            rust_samples, rust_out = measure(extra_bins["parallel_seq_rust"])
            cpp_s = summarize(cpp_samples)
            rust_s = summarize(rust_samples)
        match = "yes" if yukt_out == cpp_out == rust_out == cpp_par_out == rust_par_out else "NO"
        if match != "yes":
            print(f"{name} output mismatch")
            print("YUKT:\n", yukt_out)
            print("C++:\n", cpp_out)
            print("Rust:\n", rust_out)
        cpp_par_s = summarize(cpp_par_samples) if cpp_par_samples is not None else None
        rust_par_s = summarize(rust_par_samples) if rust_par_samples is not None else None
        rows.append(
            {
                "name": name,
                "size": SIZES[name],
                "yukt": yukt_s["median"],
                "cpp": cpp_s["median"],
                "rust": rust_s["median"],
                "cpp_par": None if cpp_par_s is None else cpp_par_s["median"],
                "rust_par": None if rust_par_s is None else rust_par_s["median"],
                "yukt_min": yukt_s["min"],
                "yukt_max": yukt_s["max"],
                "yukt_iqr": yukt_s["iqr"],
                "yukt_mad": yukt_s["mad"],
                "yukt_samples": yukt_samples,
                "cpp_samples": cpp_samples,
                "rust_samples": rust_samples,
                "cpp_par_samples": cpp_par_samples,
                "rust_par_samples": rust_par_samples,
                "match": match,
            }
        )
        claim = winner_line(
            name,
            yukt_s["median"],
            cpp_s["median"],
            rust_s["median"],
            None if cpp_par_s is None else cpp_par_s["median"],
            None if rust_par_s is None else rust_par_s["median"],
        )
        claims.append(claim)
        competitors = [cpp_s["median"], rust_s["median"]]
        if cpp_par_s is not None:
            competitors.append(cpp_par_s["median"])
        if rust_par_s is not None:
            competitors.append(rust_par_s["median"])
        if (
            match == "yes"
            and min([yukt_s["median"], *competitors]) >= 1_000_000
            and yukt_s["median"] * 1.05 < min(competitors)
        ):
            yukt_wins.append(
                f"{name} ({SIZES[name]}): YUKT {fmt_secs(yukt_s['median'])} vs fastest other "
                f"{fmt_secs(min(competitors))}."
            )
        extra = ""
        if cpp_par_s is not None and rust_par_s is not None:
            extra = (
                f"  C++-par {fmt_secs(cpp_par_s['median'])}  "
                f"Rust-par {fmt_secs(rust_par_s['median'])}"
            )
        print(
            f"  YUKT {fmt_secs(yukt_s['median'])}  C++ {fmt_secs(cpp_s['median'])}  "
            f"Rust {fmt_secs(rust_s['median'])}{extra}  match={match}"
        )
        print(f"  {claim}")

    print("measuring matmul compile time...")
    yukt_ct = compile_time(compile_cmds["matmul"][0])
    cpp_ct = compile_time(compile_cmds["matmul"][1])
    rust_ct = compile_time(compile_cmds["matmul"][2])
    compile_rows = [
        {"name": "YUKT --emit-llvm", "time": yukt_ct, "size": Path(yukt_bins["matmul"][0]).stat().st_size},
        {"name": "C++", "time": cpp_ct, "size": Path(cpp_bins["matmul"][0]).stat().st_size},
        {"name": "Rust", "time": rust_ct, "size": Path(rust_bins["matmul"][0]).stat().st_size},
    ]

    if yukt_wins:
        claims.insert(
            0,
            "Speed claim this run supports: "
            + " ".join(yukt_wins)
            + " That is not a general claim that YUKT is faster than C++ or Rust.",
        )
    else:
        claims.insert(
            0,
            "Speed claim this run supports: none. YUKT did not beat both C++ and Rust by 5% on any kernel that ran long enough to count.",
        )

    notes = [
        f"Host: {platform.machine()} {platform.system()} ({host_cpu()})",
        f"YUKT {first_line(str(yukt) + ' --version')} / LLVM IR codegen with {find_llc(clang)} -O3, linked with {clang}",
        f"C++: {cxx} -O3 -march=native; Rust: {first_line(rustc + ' --version')}",
        "YUKT timing wraps `yukt_main` (CRT `main` stripped). C++/Rust time the kernel function with `steady_clock` / `Instant`.",
        "One warmup call is discarded in-process, then the second call is timed. Median, IQR, and MAD of 15 process runs.",
        "C++ uses the same clang family YUKT links with (LLVM clang from the compiler build, not Apple clang).",
        "Keep per-host tables. Do not overwrite Apple Silicon numbers with Linux runs.",
        "C++/Rust `matmul` report sequential ijk and a row-parallel variant. YUKT auto-parallelizes when estimated work >= 1e6.",
        "C++/Rust `parallel` report sequential and explicit-thread fills. YUKT may auto-parallelize a large independent fill.",
        "A language is only called faster on a kernel when its median is at least 5% below the next one.",
        "0.08 and 0.09 YUKT medians are kept as permanent baselines in docs/PERFORMANCE_ANALYSIS_0.08.md and docs/PERFORMANCE_ANALYSIS_0.09.md.",
        "Re-run with `python3 benches/run.py` after `cmake --build build`.",
    ]
    write_report(rows, compile_rows, notes, claims)

    print("running library sort (std::sort / slice::sort_unstable), n=16000...")
    algo_cpp_s, algo_cpp_out = measure(extra_bins["algo_sort_cpp"])
    algo_rust_s, algo_rust_out = measure(extra_bins["algo_sort_rust"])
    algo_cpp = summarize(algo_cpp_s)
    algo_rust = summarize(algo_rust_s)
    algo_match = "yes" if algo_cpp_out == algo_rust_out else "NO"
    print(
        f"  std::sort C++ {fmt_secs(algo_cpp['median'])}  Rust {fmt_secs(algo_rust['median'])}  "
        f"match={algo_match}"
    )

    payload = {
        "version": "0.11",
        "baseline": "0.08",
        "baseline_0_09": "0.09",
        "host": f"{platform.machine()} {platform.system()} ({host_cpu()})",
        "os": platform.system(),
        "arch": platform.machine(),
        "cpu": host_cpu(),
        "llvm_version": first_line(clang + " --version"),
        "clang_path": clang,
        "runs": RUNS,
        "compiler_flags": {
            "yukt": f"{first_line(str(yukt) + ' --version')} --emit-llvm + {find_llc(clang)} -O3",
            "cpp": f"{cxx} -O3 -march=native -std=c++17 -pthread",
            "rust": f"{first_line(rustc + ' --version')} -C opt-level=3 -C target-cpu=native",
        },
        "baseline_0_08_ns": BASELINE_0_08,
        "baseline_0_09_ns": BASELINE_0_09,
        "kernels": [
            {
                "name": row["name"],
                "size": row["size"],
                "checksum_match": row["match"] == "yes",
                "yukt": ns_record(row["yukt_samples"]),
                "cpp_seq": ns_record(row["cpp_samples"]),
                "rust_seq": ns_record(row["rust_samples"]),
                "cpp_par": ns_record(row["cpp_par_samples"]),
                "rust_par": ns_record(row["rust_par_samples"]),
            }
            for row in rows
        ],
        "library_algorithms": {
            "sort_std_n16000": {
                "note": "std::sort / slice::sort_unstable on the same 16000-element input as the selection-sort compiler bench. Not a YUKT language feature.",
                "checksum_match": algo_match == "yes",
                "cpp": ns_record(algo_cpp_s),
                "rust": ns_record(algo_rust_s),
            }
        },
    }
    write_machine_results(payload, rows)
    print("wrote docs/BENCHMARKS.md, benchmarks/results.json, benchmarks/results.csv")
    return 0


if __name__ == "__main__":
    if "--suite=0.15" in sys.argv or os.environ.get("YUKT_BENCH_SUITE") == "0.15":
        import run_015

        sys.exit(run_015.main())
    sys.exit(main())
