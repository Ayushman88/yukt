package main

import (
	"fmt"
	"os"
	"sync"
	"time"
)

const nestDepth = 8
const nestInner = 500000
const nestMod = 1000003

func leaf() int64 {
	acc := int64(1)
	for i := int64(1); i <= nestInner; i++ {
		acc = (acc*17 + i) % nestMod
	}
	return acc
}

func nestA(n int64) int64 {
	if n < 1 {
		return leaf()
	}
	var inner int64
	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		inner = nestA(n - 1)
		wg.Done()
	}()
	wg.Wait()
	return inner + 1
}

func nestC(n int64) int64 {
	if n < 1 {
		return leaf()
	}
	return nestC(n-1) + 1
}

func main() {
	cls := "A"
	if len(os.Args) > 1 {
		cls = os.Args[1]
	}
	kernel := func() int64 { return nestA(nestDepth) }
	if cls == "C" {
		kernel = func() int64 { return nestC(nestDepth) }
	}
	_ = kernel()
	start := time.Now()
	sum := kernel()
	fmt.Fprintf(os.Stderr, "compute_ns=%d\n", time.Since(start).Nanoseconds())
	fmt.Println(sum)
}
