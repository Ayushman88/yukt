package main

import (
	"fmt"
	"os"
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
	chs := make([]chan int64, taskN)
	for i := 0; i < taskN; i++ {
		ch := make(chan int64, 1)
		chs[i] = ch
		go func(i int, ch chan int64) {
			ch <- work(int64(i))
		}(i, ch)
	}
	var sum int64
	for i := taskN - 1; i >= 0; i-- {
		sum += <-chs[i]
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
