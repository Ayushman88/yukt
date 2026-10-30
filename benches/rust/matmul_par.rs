use std::io::{self, Write};
use std::thread;
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
    let workers = thread::available_parallelism().map(|value| value.get()).unwrap_or(1);
    let chunk = ((n as usize) + workers - 1) / workers;
    thread::scope(|scope| {
        let a_ref: &[i64] = &a;
        let b_ref: &[i64] = &b;
        let mut rest: &mut [i64] = &mut c;
        let mut i0 = 0i64;
        while !rest.is_empty() {
            let rows = chunk.min((n - i0) as usize);
            let take = rows * n as usize;
            let (slice, next) = rest.split_at_mut(take);
            let start = i0;
            scope.spawn(move || {
                for r in 0..rows {
                    let i = start + r as i64;
                    for j in 0..n {
                        let mut total = 0i64;
                        for k in 0..n {
                            total += a_ref[(i * n + k) as usize] * b_ref[(k * n + j) as usize];
                        }
                        slice[r * n as usize + j as usize] = total;
                    }
                }
            });
            i0 += rows as i64;
            rest = next;
        }
    });
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
