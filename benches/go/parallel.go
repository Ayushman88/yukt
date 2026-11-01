package main

import (
	"fmt"
	"os"
	"time"
)

func kernel() int64 {
	const n int64 = 20000000
	items := make([]int64, n)
	for i := int64(0); i < n; i++ {
		items[i] = i*3 + 1
	}
	var total int64
	for i := int64(0); i < n; i++ {
		total += items[i]
	}
	return total
}

func main() {
	_ = kernel()
	start := time.Now()
	total := kernel()
	fmt.Fprintf(os.Stderr, "compute_ns=%d\n", time.Since(start).Nanoseconds())
	fmt.Println(total)
}
