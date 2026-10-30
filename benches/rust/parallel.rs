use std::io::{self, Write};
use std::thread;
use std::time::Instant;

fn kernel() -> i64 {
    let n: i64 = 20_000_000;
    let mut items = vec![0i64; n as usize];
    let workers = thread::available_parallelism().map(|value| value.get()).unwrap_or(1);
    let chunk = (n as usize + workers - 1) / workers;
    thread::scope(|scope| {
        let mut rest: &mut [i64] = &mut items;
        let mut begin = 0i64;
        while !rest.is_empty() {
            let take = chunk.min(rest.len());
            let (slice, next) = rest.split_at_mut(take);
            let start = begin;
            scope.spawn(move || {
                for (offset, slot) in slice.iter_mut().enumerate() {
                    let i = start + offset as i64;
                    *slot = i * 3 + 1;
                }
            });
            begin += take as i64;
            rest = next;
        }
    });
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
