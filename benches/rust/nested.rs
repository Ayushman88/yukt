use std::hint::black_box;
use std::io::{self, Write};
use std::time::Instant;

fn kernel(n: i64) -> i64 {
    let mut items = vec![0i64; n as usize];
    let last = n - 1;
    for i in 0..=last {
        for j in 0..=last {
            items[j as usize] += i;
        }
    }
    items.iter().sum()
}

fn main() {
    let n = black_box(8000i64);
    let _ = kernel(n);
    let started = Instant::now();
    let checksum = kernel(n);
    let ns = started.elapsed().as_nanos();
    let _ = writeln!(io::stderr(), "compute_ns={ns}");
    println!("{checksum}");
}
