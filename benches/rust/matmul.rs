use std::io::{self, Write};
use std::time::Instant;

fn kernel() -> i64 {
    let n: i64 = 512;
    let size = (n * n) as usize;
    let mut a = vec![0i64; size];
    let mut b = vec![0i64; size];
    let mut c = vec![0i64; size];
    for i in 0..n {
        for j in 0..n {
            a[(i * n + j) as usize] = i + j;
            b[(i * n + j) as usize] = i - j + 1;
        }
    }
    for i in 0..n {
        for j in 0..n {
            let mut total = 0i64;
            for k in 0..n {
                total += a[(i * n + k) as usize] * b[(k * n + j) as usize];
            }
            c[(i * n + j) as usize] = total;
        }
    }
    c.iter().sum()
}

fn main() {
    let _ = kernel();
    let started = Instant::now();
    let checksum = kernel();
    let ns = started.elapsed().as_nanos();
    let _ = writeln!(io::stderr(), "compute_ns={ns}");
    println!("{checksum}");
}
