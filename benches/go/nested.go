package main

import (
	"fmt"
	"os"
	"time"
)

func kernel(n int64) int64 {
	items := make([]int64, n)
	last := n - 1
	for i := int64(0); i <= last; i++ {
		for j := int64(0); j <= last; j++ {
			items[j] += i
		}
	}
	var checksum int64
	for j := int64(0); j <= last; j++ {
		checksum += items[j]
	}
	return checksum
}

func main() {
	n := int64(8000 + (len(os.Args) - 1))
	_ = kernel(n)
	start := time.Now()
	checksum := kernel(n)
	fmt.Fprintf(os.Stderr, "compute_ns=%d\n", time.Since(start).Nanoseconds())
	fmt.Println(checksum)
}
