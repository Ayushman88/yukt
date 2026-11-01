package main

import (
	"fmt"
	"os"
	"time"
)

func kernel() int64 {
	const n int64 = 512
	cells := n * n
	a := make([]int64, cells)
	b := make([]int64, cells)
	c := make([]int64, cells)
	for i := int64(0); i < n; i++ {
		for j := int64(0); j < n; j++ {
			a[i*n+j] = i + j
			b[i*n+j] = i - j + 1
		}
	}
	for i := int64(0); i < n; i++ {
		for j := int64(0); j < n; j++ {
			var total int64
			for k := int64(0); k < n; k++ {
				total += a[i*n+k] * b[k*n+j]
			}
			c[i*n+j] = total
		}
	}
	var checksum int64
	for i := int64(0); i < cells; i++ {
		checksum += c[i]
	}
	return checksum
}

func main() {
	_ = kernel()
	start := time.Now()
	checksum := kernel()
	fmt.Fprintf(os.Stderr, "compute_ns=%d\n", time.Since(start).Nanoseconds())
	fmt.Println(checksum)
}
