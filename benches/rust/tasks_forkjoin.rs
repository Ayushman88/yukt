use std::env;
use std::io::{self, Write};
use std::thread;
use std::time::Instant;

const TASK_N: i64 = 16;
const TASK_INNER: i64 = 500000;
const TASK_MOD: i64 = 1000003;

fn work(n: i64) -> i64 {
    let mut acc = n + 1;
    for i in 1..=TASK_INNER {
        acc = (acc * 17 + i) % TASK_MOD;
    }
    acc
}

fn class_b() -> i64 {
    let mut handles = Vec::new();
    for i in 0..TASK_N {
        handles.push(thread::spawn(move || work(i)));
    }
    let mut sum = 0i64;
    for handle in handles.into_iter().rev() {
        sum += handle.join().unwrap();
    }
    sum
}

fn class_c() -> i64 {
    let mut sum = 0i64;
    for i in 0..TASK_N {
        sum += work(i);
    }
    sum
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
