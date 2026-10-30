use std::hint::black_box;
use std::io::{self, Write};
use std::time::Instant;

fn fib(n: i64) -> i64 {
    if n < 2 {
        return n;
    }
    let mut a = 0i64;
    let mut b = 1i64;
    for _ in 2..=n {
        let next = a + b;
        a = b;
        b = next;
    }
    b
}

fn kernel() -> i64 {
    let arg = black_box(45i64);
    let mut acc = 0i64;
    for _ in 1..=1_500_000 {
        acc += fib(arg);
    }
    acc
}

fn main() {
    let _ = kernel();
    let started = Instant::now();
    let acc = kernel();
    let ns = started.elapsed().as_nanos();
    let _ = writeln!(io::stderr(), "compute_ns={ns}");
    println!("{acc}");
}
