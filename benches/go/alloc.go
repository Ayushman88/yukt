package main

import (
	"fmt"
	"os"
	"time"
)

func kernel() (int64, int64, int64) {
	const n int64 = 4000000
	const passes int64 = 25
	items := make([]int64, n)
	for p := int64(1); p <= passes; p++ {
		for i := int64(0); i < n; i++ {
			items[i] = i + p
		}
	}
	return items[0], items[n-1], n
}

func main() {
	_, _, _ = kernel()
	start := time.Now()
	first, last, count := kernel()
	fmt.Fprintf(os.Stderr, "compute_ns=%d\n", time.Since(start).Nanoseconds())
	fmt.Println(first)
	fmt.Println(last)
	fmt.Println(count)
}
