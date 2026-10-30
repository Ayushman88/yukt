use std::io::{self, Write};
use std::time::Instant;

fn kernel() -> (i64, i64) {
    let n: i64 = 16000;
    let mut items: Vec<i64> = (0..n).map(|i| (n - i) * 17 + i).collect();
    items.sort_unstable();
    (items[0], items[(n - 1) as usize])
}

fn main() {
    let _ = kernel();
    let started = Instant::now();
    let (first, last) = kernel();
    let ns = started.elapsed().as_nanos();
    let _ = writeln!(io::stderr(), "compute_ns={ns}");
    println!("{first}");
    println!("{last}");
}
