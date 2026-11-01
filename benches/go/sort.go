package main

import (
	"fmt"
	"os"
	"time"
)

func kernel() (int64, int64) {
	const n int64 = 16000
	items := make([]int64, n)
	for i := int64(0); i < n; i++ {
		items[i] = (n-i)*17 + i
	}
	end := n - 2
	for i := int64(0); i <= end; i++ {
		min := i
		for j := i + 1; j < n; j++ {
			if items[j] < items[min] {
				min = j
			}
		}
		items[i], items[min] = items[min], items[i]
	}
	return items[0], items[n-1]
}

func main() {
	_, _ = kernel()
	start := time.Now()
	first, last := kernel()
	fmt.Fprintf(os.Stderr, "compute_ns=%d\n", time.Since(start).Nanoseconds())
	fmt.Println(first)
	fmt.Println(last)
}
