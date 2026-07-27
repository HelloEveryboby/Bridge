// Go 示例模块
//
// 编译:
//   cd modules/go
//   go build -buildmode=c-shared -o libgomath.so gomath.go
//
// 注意: Go 编译的 .so 包含 Go runtime，体积较大 (~2MB)
// 但 goroutine、GC 等特性可以正常使用

package main

import "C"
import (
	"math"
	"strings"
	"unsafe"
)

// ── 函数定义 ──────────────────────────────────────────────────────

//export g_add
func g_add(a, b C.double) C.double {
	return C.double(float64(a) + float64(b))
}

//export g_sub
func g_sub(a, b C.double) C.double {
	return C.double(float64(a) - float64(b))
}

//export g_mul
func g_mul(a, b C.double) C.double {
	return C.double(float64(a) * float64(b))
}

//export g_div
func g_div(a, b C.double) C.double {
	if float64(b) != 0 {
		return C.double(float64(a) / float64(b))
	}
	return 0
}

//export g_sqrt
func g_sqrt(x C.double) C.double {
	return C.double(math.Sqrt(float64(x)))
}

//export g_pow
func g_pow(base, exp C.double) C.double {
	return C.double(math.Pow(float64(base), float64(exp)))
}

//export g_sin
func g_sin(x C.double) C.double {
	return C.double(math.Sin(float64(x)))
}

//export g_cos
func g_cos(x C.double) C.double {
	return C.double(math.Cos(float64(x)))
}

//export g_log
func g_log(x C.double) C.double {
	return C.double(math.Log(float64(x)))
}

//export g_abs
func g_abs(x C.double) C.double {
	return C.double(math.Abs(float64(x)))
}

//export g_factorial
func g_factorial(n C.int) C.int {
	if int(n) <= 1 {
		return 1
	}
	result := 1
	for i := 2; i <= int(n); i++ {
		result *= i
	}
	return C.int(result)
}

//export g_fibonacci
func g_fibonacci(n C.int) C.int {
	if int(n) <= 0 {
		return 0
	}
	if int(n) == 1 {
		return 1
	}
	a, b := 0, 1
	for i := 2; i <= int(n); i++ {
		a, b = b, a+b
	}
	return C.int(b)
}

//export g_is_prime
func g_is_prime(n C.int) bool {
	x := int(n)
	if x < 2 {
		return false
	}
	if x < 4 {
		return true
	}
	if x%2 == 0 || x%3 == 0 {
		return false
	}
	for i := 5; i*i <= x; i += 6 {
		if x%i == 0 || x%(i+2) == 0 {
			return false
		}
	}
	return true
}

//export g_reverse
func g_reverse(s *C.char) *C.char {
	goStr := C.GoString(s)
	runes := []rune(goStr)
	for i, j := 0, len(runes)-1; i < j; i, j = i+1, j-1 {
		runes[i], runes[j] = runes[j], runes[i]
	}
	return C.CString(string(runes))
}

//export g_upper
func g_upper(s *C.char) *C.char {
	return C.CString(strings.ToUpper(C.GoString(s)))
}

//export g_version
func g_version() *C.char {
	return C.CString("gomath 1.0.0 (Go)")
}

// ── 模块元数据 ────────────────────────────────────────────────────

var infoJSON = `{"name":"gomath","version":"1.0.0","language":"go","functions":[{"name":"g_add","signature":"dd>d"},{"name":"g_sub","signature":"dd>d"},{"name":"g_mul","signature":"dd>d"},{"name":"g_div","signature":"dd>d"},{"name":"g_sqrt","signature":"d>d"},{"name":"g_pow","signature":"dd>d"},{"name":"g_sin","signature":"d>d"},{"name":"g_cos","signature":"d>d"},{"name":"g_log","signature":"d>d"},{"name":"g_abs","signature":"d>d"},{"name":"g_factorial","signature":"i>i"},{"name":"g_fibonacci","signature":"i>i"},{"name":"g_is_prime","signature":"i>b"},{"name":"g_reverse","signature":"s>s"},{"name":"g_upper","signature":"s>s"},{"name":"g_version","signature":"v>s"}]}`

//export pb_module_info
func pb_module_info() *C.char {
	return C.CString(infoJSON)
}

//export pb_module_func
func pb_module_func(name *C.char) unsafe.Pointer {
	goName := C.GoString(name)
	switch goName {
	case "g_add":
		return C.g_add
	case "g_sub":
		return C.g_sub
	case "g_mul":
		return C.g_mul
	case "g_div":
		return C.g_div
	case "g_sqrt":
		return C.g_sqrt
	case "g_pow":
		return C.g_pow
	case "g_sin":
		return C.g_sin
	case "g_cos":
		return C.g_cos
	case "g_log":
		return C.g_log
	case "g_abs":
		return C.g_abs
	case "g_factorial":
		return C.g_factorial
	case "g_fibonacci":
		return C.g_fibonacci
	case "g_is_prime":
		return C.g_is_prime
	case "g_reverse":
		return C.g_reverse
	case "g_upper":
		return C.g_upper
	case "g_version":
		return C.g_version
	}
	return nil
}

func main() {}
