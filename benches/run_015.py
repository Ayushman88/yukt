#!/usr/bin/env python3
"""0.15 suite: YUKT / C / C++ / Rust / Go. Does not write results-0.11.json."""

from __future__ import annotations

import json
import os
import platform
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run as bench011

ROOT = bench011.ROOT
BENCH = bench011.BENCH
OUT = BENCH / "out"
COMPUTE = ["nested", "matmul", "fibonacci", "sort", "alloc", "parallel"]
TASKS = ["tasks_forkjoin", "tasks_nested"]
LIMITATIONS = [
    "Running-wait cycles can hang; 0.14.1 has no deadlock detector.",
    "macOS YUKT Class A is libdispatch, not pthreads.",
    "Linux YUKT tasks use dedicated persistent pthread workers (<=32), not one pthread_create per start.",
    "Class A C uses a persistent pool (ncpu capped at 32). Class B is thread-per-task and is not a Class A peer.",
    "Go Class A is the Go scheduler, not a pthread pool.",
    "The heap is one recursive mutex; allocating task bodies serialize.",
    "GitHub Actions times are not publication-quality.",
    "Automatic repeat parallelization is not the start/wait API.",
    "YUKT parallel fill may auto-par; the reduction is sequential (if-guarded) and is not ranked with seq or explicit-thread rows.",
    "0.11 numbers are a different compiler version and must not be subtracted as a 0.15 speedup.",
    "No GPU / CUDA numbers.",
]


def host_id() -> str:
    system = platform.system().lower()
    machine = platform.machine().lower()
    if system == "darwin":
        return "macos-arm64" if machine in {"arm64", "aarch64"} else f"macos-{machine}"
    if system == "linux":
        return "linux-x86-64" if machine in {"x86_64", "amd64"} else f"linux-{machine}"
    return f"{system}-{machine}"


def ncpu() -> int:
    return os.cpu_count() or 1


def compile_c(clang: str, src: Path, dest: Path, extra: list[str] | None = None) -> list[str]:
    cmd = [
        clang,
        "-O3",
        "-march=native",
        "-std=c11",
        "-pthread",
        "-I",
        str(BENCH / "c"),
        str(src),
        "-o",
        str(dest),
    ]
    if extra:
        cmd[1:1] = extra
    sysroot = bench011.macos_sysroot()
    if sysroot:
        cmd.extend(["-isysroot", sysroot])
    built = bench011.run(cmd)
    if built.returncode != 0:
        raise RuntimeError(f"C compile failed: {' '.join(cmd)}\n{built.stdout}\n{built.stderr}")
    return [str(dest)]


def compile_go(go: str, src: Path, dest: Path) -> list[str]:
    cmd = [go, "build", "-o", str(dest), str(src)]
    built = bench011.run(cmd)
    if built.returncode != 0:
        raise RuntimeError(f"Go compile failed: {' '.join(cmd)}\n{built.stdout}\n{built.stderr}")
    return [str(dest)]


def compile_cpp(cxx: str, src: Path, dest: Path) -> list[str]:
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
    sysroot = bench011.macos_sysroot()
    if sysroot and "llvm" in cxx:
        cmd.extend(["-isysroot", sysroot])
    built = bench011.run(cmd)
    if built.returncode != 0:
        raise RuntimeError(f"C++ compile failed: {' '.join(cmd)}\n{built.stdout}\n{built.stderr}")
    return [str(dest)]


def compile_rust(rustc: str, src: Path, dest: Path) -> list[str]:
    cmd = [rustc, "-C", "opt-level=3", "-C", "target-cpu=native", str(src), "-o", str(dest)]
    built = bench011.run(cmd)
    if built.returncode != 0:
        raise RuntimeError(f"Rust compile failed: {' '.join(cmd)}\n{built.stdout}\n{built.stderr}")
    return [str(dest)]


def winner_for(name: str, samples: dict[str, list[int]], match: bool) -> str:
    if not match:
        return f"{name}: no speed claim — checksum mismatch"
    ranked: list[tuple[str, float]] = []
    for lang, values in samples.items():
        ranked.append((lang, bench011.summarize(values)["median"]))
    ranked.sort(key=lambda item: item[1])
    labels = ", ".join(f"{lang} {bench011.fmt_secs(median)}" for lang, median in ranked)
    if any(median < 1_000_000 for _, median in ranked):
        return f"{name}: no speed claim — a median was under 1ms ({labels})"
    if len(ranked) < 2:
        return f"{name}: {labels} (single peer; no comparison)"
    best_name, best = ranked[0]
    second_name, second = ranked[1]
    if best * 1.05 >= second:
        return (
            f"{name}: too close to call — {best_name} {bench011.fmt_secs(best)} vs "
            f"{second_name} {bench011.fmt_secs(second)}. Full order: {labels}."
        )
    return (
        f"{name}: {best_name} {bench011.fmt_secs(best)} vs {second_name} {bench011.fmt_secs(second)} "
        f"({second / best:.2f}×). Full order: {labels}."
    )


def main() -> int:
    runs = int(os.environ.get("BENCH_RUNS", bench011.RUNS))
    bench011.RUNS = runs
    publication = os.environ.get("YUKT_BENCH_PUBLICATION", "true").lower() != "false"
    if "--publication=false" in sys.argv:
        publication = False

    yukt = Path(os.environ.get("YUKT", ROOT / "build" / "yukt"))
    runtime = Path(os.environ.get("YUKT_RUNTIME_LIB", ROOT / "build" / "libyukt_runtime.a"))
    if not yukt.exists() or not runtime.exists():
        print("missing yukt or libyukt_runtime.a", file=sys.stderr)
        return 1

    clang = bench011.find_clang()
    cxx = bench011.find_cxx()
    rustc = shutil.which("rustc")
    go = shutil.which("go")
    OUT.mkdir(parents=True, exist_ok=True)

    skipped: dict[str, str] = {}
    if rustc is None:
        skipped["rust"] = "rustc is not on PATH"
    if go is None:
        skipped["go"] = "go is not on PATH"

    print(f"0.15 suite host={host_id()} runs={runs} publication={publication}")
    print("compiling compute kernels...")

    bins: dict[str, dict[str, list[str]]] = {name: {} for name in COMPUTE}
    for name in COMPUTE:
        bins[name]["yukt"] = bench011.compile_yukt_kernel(yukt, clang, name, runtime)
        bins[name]["c"] = compile_c(clang, BENCH / "c" / f"{name}.c", OUT / f"c_{name}")
        bins[name]["cpp"] = compile_cpp(cxx, BENCH / "cpp" / f"{name}.cpp", OUT / f"cpp_{name}")
        if rustc is not None:
            bins[name]["rust"] = compile_rust(rustc, BENCH / "rust" / f"{name}.rs", OUT / f"rust_{name}")
        if go is not None:
            bins[name]["go"] = compile_go(go, BENCH / "go" / f"{name}.go", OUT / f"go_{name}")

    extra: dict[str, list[str]] = {}
    extra["c_parallel_par"] = compile_c(clang, BENCH / "c" / "parallel_par.c", OUT / "c_parallel_par")
    extra["cpp_parallel_par"] = compile_cpp(cxx, BENCH / "cpp" / "parallel.cpp", OUT / "cpp_parallel_par")
    extra["cpp_parallel_seq"] = compile_cpp(cxx, BENCH / "cpp" / "parallel_seq.cpp", OUT / "cpp_parallel_seq")
    if rustc is not None:
        extra["rust_parallel_par"] = compile_rust(
            rustc, BENCH / "rust" / "parallel.rs", OUT / "rust_parallel_par"
        )
        extra["rust_parallel_seq"] = compile_rust(
            rustc, BENCH / "rust" / "parallel_seq.rs", OUT / "rust_parallel_seq"
        )

    kernels_out: list[dict[str, object]] = []
    for name in COMPUTE:
        print(f"running {name}...")
        langs = ["yukt", "c", "cpp"]
        if "rust" in bins[name]:
            langs.append("rust")
        if "go" in bins[name]:
            langs.append("go")
        samples: dict[str, list[int]] = {}
        stdout: dict[str, str] = {}
        for lang in langs:
            cmd = bins[name][lang]
            if name == "parallel" and lang == "cpp":
                cmd = extra["cpp_parallel_seq"]
            if name == "parallel" and lang == "rust":
                cmd = extra["rust_parallel_seq"]
            samples[lang], stdout[lang] = bench011.measure(cmd)
        if name == "parallel":
            samples["c_par"], stdout["c_par"] = bench011.measure(extra["c_parallel_par"])
            samples["cpp_par"], stdout["cpp_par"] = bench011.measure(extra["cpp_parallel_par"])
            if "rust_parallel_par" in extra:
                samples["rust_par"], stdout["rust_par"] = bench011.measure(extra["rust_parallel_par"])
        texts = list(stdout.values())
        match = all(text == texts[0] for text in texts)
        if not match:
            print(f"{name} output mismatch")
            for lang, text in stdout.items():
                print(f"{lang}: {text!r}")
        comparisons: dict[str, object] | None = None
        if name == "parallel":
            seq = {
                lang: values
                for lang, values in samples.items()
                if lang != "yukt" and not lang.endswith("_par")
            }
            auto_par = {"yukt": samples["yukt"]} if "yukt" in samples else {}
            explicit = {
                lang: values
                for lang, values in samples.items()
                if lang.endswith("_par")
            }
            seq_line = winner_for(f"{name} seq", seq, match)
            auto_line = winner_for(f"{name} auto-par", auto_par, match)
            exp_line = winner_for(f"{name} explicit-thread", explicit, match)
            claim = f"{seq_line} | {auto_line} | {exp_line}"
            comparisons = {
                "seq": {"winner_line": seq_line, "languages": list(seq)},
                "auto_par": {
                    "winner_line": auto_line,
                    "languages": list(auto_par),
                    "note": "YUKT automatic repeat parallelization of the fill; reduction is sequential.",
                },
                "explicit_thread": {
                    "winner_line": exp_line,
                    "languages": list(explicit),
                    "note": "Thread-per-chunk fill + sequential reduce. Not YUKT auto-par and not Class A.",
                },
            }
        else:
            claim = winner_for(name, samples, match)
        row: dict[str, object] = {
            "name": name,
            "size": bench011.SIZES[name],
            "checksum_match": match,
            "class": "compute",
            "winner_line": claim,
        }
        if comparisons is not None:
            row["comparisons"] = comparisons
        for lang, values in samples.items():
            row[lang] = bench011.ns_record(values)
        kernels_out.append(row)
        yukt_s = bench011.summarize(samples["yukt"])
        print(f"  YUKT {bench011.fmt_secs(yukt_s['median'])} match={'yes' if match else 'NO'}")
        print(f"  {claim}")

    print("compiling task kernels...")
    task_bins: dict[str, dict[str, list[str]]] = {}
    for name in TASKS:
        task_bins[name] = {
            "yukt": bench011.compile_yukt_kernel(yukt, clang, name, runtime),
            "c": compile_c(clang, BENCH / "c" / f"{name}.c", OUT / f"c_{name}"),
            "cpp": compile_cpp(cxx, BENCH / "cpp" / f"{name}.cpp", OUT / f"cpp_{name}"),
        }
        task_bins[name]["yukt_seq"] = bench011.compile_yukt_kernel(
            yukt, clang, f"{name}_seq", runtime
        )
        if rustc is not None:
            task_bins[name]["rust"] = compile_rust(rustc, BENCH / "rust" / f"{name}.rs", OUT / f"rust_{name}")
        if go is not None:
            task_bins[name]["go"] = compile_go(go, BENCH / "go" / f"{name}.go", OUT / f"go_{name}")

    for name in TASKS:
        print(f"running {name}...")
        class_cmds: dict[str, dict[str, list[str]]] = {
            "A": {
                "yukt": task_bins[name]["yukt"],
                "c": task_bins[name]["c"] + ["A"],
            },
            "B": {
                "c": task_bins[name]["c"] + ["B"],
                "cpp": task_bins[name]["cpp"] + ["B"],
            },
            "C": {
                "yukt": task_bins[name]["yukt_seq"],
                "c": task_bins[name]["c"] + ["C"],
                "cpp": task_bins[name]["cpp"] + ["C"],
            },
        }
        if "go" in task_bins[name]:
            class_cmds["A"]["go"] = task_bins[name]["go"] + ["A"]
            class_cmds["C"]["go"] = task_bins[name]["go"] + ["C"]
        if "rust" in task_bins[name]:
            class_cmds["B"]["rust"] = task_bins[name]["rust"] + ["B"]
            class_cmds["C"]["rust"] = task_bins[name]["rust"] + ["C"]

        classes_out: dict[str, object] = {}
        for cls, cmds in class_cmds.items():
            samples = {}
            stdout = {}
            for lang, cmd in cmds.items():
                samples[lang], stdout[lang] = bench011.measure(cmd)
            texts = list(stdout.values())
            match = bool(texts) and all(text == texts[0] for text in texts)
            if not match:
                print(f"{name} class {cls} output mismatch")
                for lang, text in stdout.items():
                    print(f"  {lang}: {text!r}")
            claim = winner_for(f"{name} class {cls}", samples, match)
            entry: dict[str, object] = {"checksum_match": match, "winner_line": claim}
            for lang, values in samples.items():
                entry[lang] = bench011.ns_record(values)
            classes_out[cls] = entry
            print(f"  class {cls} match={'yes' if match else 'NO'}")
            print(f"  {claim}")
        kernels_out.append(
            {
                "name": name,
                "size": "N=16 INNER=500000 mix" if name == "tasks_forkjoin" else "depth=8 INNER=500000 mix",
                "class": "tasks",
                "task_n": 16 if name == "tasks_forkjoin" else 8,
                "classes": classes_out,
            }
        )

    print("usability program (compile + one run, not a speed claim)...")
    usability_ok = False
    usability_out = ""
    try:
        built = bench011.run([str(yukt), "run", str(BENCH / "yukt" / "usability.yukt")])
        usability_ok = built.returncode == 0 and built.stdout.strip() == "4"
        usability_out = built.stdout.strip()
        if not usability_ok:
            print(f"usability failed: {built.stdout}\n{built.stderr}", file=sys.stderr)
    except RuntimeError as error:
        usability_out = str(error)

    yukt_build_s = bench011.compile_time(
        [str(yukt), "build", str(BENCH / "yukt" / "matmul.yukt"), "-o", str(OUT / "yukt_matmul_product")]
    )

    hid = host_id()
    payload = {
        "version": "0.15",
        "method": "docs/BENCHMARK_METHOD_015.md",
        "publication": publication,
        "host_id": hid,
        "host": f"{platform.machine()} {platform.system()} ({bench011.host_cpu()})",
        "os": platform.system(),
        "arch": platform.machine(),
        "cpu": bench011.host_cpu(),
        "kernel": bench011.first_line("uname -srm"),
        "ncpu": ncpu(),
        "runs": runs,
        "yukt_version": bench011.first_line(str(yukt) + " --version"),
        "llvm_version": bench011.first_line(clang + " --version"),
        "clang_path": clang,
        "c_compiler": f"{bench011.first_line(clang + ' --version')} -O3 -march=native -std=c11 -pthread",
        "cxx_compiler": f"{bench011.first_line(cxx + ' --version')} -O3 -march=native -std=c++17 -pthread",
        "rustc_version": None if rustc is None else bench011.first_line(rustc + " --version"),
        "go_version": None if go is None else bench011.first_line(go + " version"),
        "GOMAXPROCS": os.environ.get("GOMAXPROCS", str(ncpu())),
        "compiler_flags": {
            "yukt": f"{bench011.first_line(str(yukt) + ' --version')} --emit-llvm + {bench011.find_llc(clang)} -O3",
            "yukt_build": "yukt build (product compile, not kernel compute_ns)",
            "c": f"{clang} -O3 -march=native -std=c11 -pthread",
            "cpp": f"{cxx} -O3 -march=native -std=c++17 -pthread",
            "rust": None
            if rustc is None
            else f"{bench011.first_line(rustc + ' --version')} -C opt-level=3 -C target-cpu=native",
            "go": None if go is None else f"{bench011.first_line(go + ' version')} go build",
        },
        "skipped": skipped,
        "yukt_build_matmul_s": yukt_build_s,
        "usability": {"ok": usability_ok, "stdout": usability_out},
        "kernels": kernels_out,
        "limitations": LIMITATIONS,
    }

    out_path = ROOT / "benchmarks" / f"results-0.15.{hid}.json"
    if not publication:
        out_path = ROOT / "benchmarks" / f"results-0.15.ci-{hid}.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    index_path = ROOT / "benchmarks" / "results-0.15.json"
    index = {"version": "0.15", "hosts": {}, "method": "docs/BENCHMARK_METHOD_015.md"}
    if index_path.exists():
        try:
            index = json.loads(index_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            pass
    index.setdefault("hosts", {})
    index["hosts"][hid] = {"file": out_path.name, "publication": publication}
    index_path.write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out_path} (did not write results-0.11.json)")
    kernels_ok = all(
        k.get("checksum_match", True) is True
        and all(cls.get("checksum_match", True) for cls in (k.get("classes") or {}).values())
        for k in kernels_out
    )
    return 0 if kernels_ok and usability_ok else 1


if __name__ == "__main__":
    sys.exit(main())
