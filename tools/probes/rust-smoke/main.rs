// Copyright 2026, haiku-odroid-c2 port authors.
// Distributed under the terms of the MIT License.
//
// rust-smoke: the aarch64-unknown-haiku Rust standard library on the C2
// (milestone M78): threads, channels, files, time, environment, a caught panic,
// 128-bit and floating point arithmetic.
// Build: . deps/firefox/rust-out/env.sh; rustc -O --target aarch64-unknown-haiku
//     -C linker=$CARGO_TARGET_AARCH64_UNKNOWN_HAIKU_LINKER main.rs -o rust-smoke

use std::io::{Read, Write};
use std::sync::mpsc;
use std::thread;
use std::time::Instant;

fn main() {
    let start = Instant::now();
    let (tx, rx) = mpsc::channel();
    let handles: Vec<_> = (0..4u64)
        .map(|i| {
            let tx = tx.clone();
            thread::spawn(move || {
                let sum: u64 = (0..1_000_000u64).map(|n| n * (i + 1)).sum();
                tx.send((i, sum)).unwrap();
            })
        })
        .collect();
    drop(tx);
    for h in handles {
        h.join().unwrap();
    }
    let mut results: Vec<_> = rx.iter().collect();
    results.sort();
    for (i, sum) in &results {
        println!("thread {i}: {sum}");
    }

    let path = std::env::temp_dir().join("rust-smoke.txt");
    std::fs::File::create(&path).unwrap().write_all(b"hello from Rust").unwrap();
    let mut text = String::new();
    std::fs::File::open(&path).unwrap().read_to_string(&mut text).unwrap();
    std::fs::remove_file(&path).unwrap();
    println!("file: {text}");

    let caught = std::panic::catch_unwind(|| panic!("on purpose"));
    println!("panic caught: {}", caught.is_err());

    let big: u128 = u128::MAX / 3;
    println!("u128: {big}, sqrt(2): {:.6}", 2f64.sqrt());
    println!("HOME: {:?}", std::env::var("HOME").ok());
    println!("took {:?}", start.elapsed());
}
