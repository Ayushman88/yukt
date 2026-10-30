use std::io::{self, Write};
use std::time::Instant;

fn kernel() -> (i64, i64) {
    let n: i64 = 16000;
    let mut items: Vec<i64> = (0..n).map(|i| (n - i) * 17 + i).collect();
    let last = n - 2;
    for i in 0..=last {
        let mut min = i;
        for j in (i + 1)..n {
            if items[j as usize] < items[min as usize] {
                min = j;
            }
        }
        items.swap(i as usize, min as usize);
    }
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
