use std::env;
use std::io::{self, Write};
use std::thread;
use std::time::Instant;

const DEPTH: i64 = 8;
const INNER: i64 = 500000;
const MIX_MOD: i64 = 1000003;

fn leaf() -> i64 {
    let mut acc = 1i64;
    for i in 1..=INNER {
        acc = (acc * 17 + i) % MIX_MOD;
    }
    acc
}

fn nest_b(n: i64) -> i64 {
    if n < 1 {
        return leaf();
    }
    let child = thread::spawn(move || nest_b(n - 1));
    child.join().unwrap() + 1
}

fn nest_c(n: i64) -> i64 {
    if n < 1 {
        return leaf();
    }
    nest_c(n - 1) + 1
}

fn class_b() -> i64 {
    nest_b(DEPTH)
}

fn class_c() -> i64 {
    nest_c(DEPTH)
}

fn main() {
    let cls = env::args().nth(1).unwrap_or_else(|| "C".to_string());
    let kernel: fn() -> i64 = if cls == "B" { class_b } else { class_c };
    let _ = kernel();
    let started = Instant::now();
    let sum = kernel();
    let _ = writeln!(io::stderr(), "compute_ns={}", started.elapsed().as_nanos());
    println!("{sum}");
}
