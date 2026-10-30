use std::io::{self, Write};
use std::time::Instant;

fn kernel() -> i64 {
    let n: i64 = 20_000_000;
    let mut items = vec![0i64; n as usize];
    for (i, slot) in items.iter_mut().enumerate() {
        *slot = i as i64 * 3 + 1;
    }
    items.iter().sum()
}

fn main() {
    let _ = kernel();
    let started = Instant::now();
    let total = kernel();
    let ns = started.elapsed().as_nanos();
    let _ = writeln!(io::stderr(), "compute_ns={ns}");
    println!("{total}");
}
