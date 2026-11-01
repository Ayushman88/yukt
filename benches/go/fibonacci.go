package main

import (
	"fmt"
	"os"
	"time"
)

func fib(n int64) int64 {
	if n < 2 {
		return n
	}
	a := int64(0)
	b := int64(1)
	for i := int64(2); i <= n; i++ {
		next := a + b
		a = b
		b = next
	}
	return b
}

func kernel() int64 {
	arg := int64(len(os.Args)-1) + 45
	var acc int64
	for t := int64(1); t <= 1500000; t++ {
		acc += fib(arg)
	}
	return acc
}

func main() {
	_ = kernel()
	start := time.Now()
	acc := kernel()
	fmt.Fprintf(os.Stderr, "compute_ns=%d\n", time.Since(start).Nanoseconds())
	fmt.Println(acc)
}
