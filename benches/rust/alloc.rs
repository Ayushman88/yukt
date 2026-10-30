use std::io::{self, Write};
use std::time::Instant;

fn kernel() -> (i64, i64, i64) {
    let n: i64 = 4_000_000;
    let passes: i64 = 25;
    let mut items = vec![0i64; n as usize];
    for p in 1..=passes {
        for i in 0..n {
            items[i as usize] = i + p;
        }
    }
    (items[0], items[(n - 1) as usize], items.len() as i64)
}

fn main() {
    let _ = kernel();
    let started = Instant::now();
    let (first, last, count) = kernel();
    let ns = started.elapsed().as_nanos();
    let _ = writeln!(io::stderr(), "compute_ns={ns}");
    println!("{first}");
    println!("{last}");
    println!("{count}");
}
