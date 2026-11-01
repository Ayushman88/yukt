package main

import (
	"fmt"
	"os"
	"sync"
	"time"
)

const taskN = 16
const taskInner = 500000
const taskMod = 1000003

func work(n int64) int64 {
	acc := n + 1
	for i := int64(1); i <= taskInner; i++ {
		acc = (acc*17 + i) % taskMod
	}
	return acc
}

func classA() int64 {
	outs := make([]int64, taskN)
	var wg sync.WaitGroup
	wg.Add(taskN)
	for i := 0; i < taskN; i++ {
		go func(i int) {
			outs[i] = work(int64(i))
			wg.Done()
		}(i)
	}
	wg.Wait()
	var sum int64
	for i := taskN - 1; i >= 0; i-- {
		sum += outs[i]
	}
	return sum
}

func classC() int64 {
	var sum int64
	for i := 0; i < taskN; i++ {
		sum += work(int64(i))
	}
	return sum
}

func main() {
	cls := "A"
	if len(os.Args) > 1 {
		cls = os.Args[1]
	}
	kernel := classA
	if cls == "C" {
		kernel = classC
	}
	_ = kernel()
	start := time.Now()
	sum := kernel()
	fmt.Fprintf(os.Stderr, "compute_ns=%d\n", time.Since(start).Nanoseconds())
	fmt.Println(sum)
}
