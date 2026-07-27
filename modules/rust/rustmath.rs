/// Rust 示例模块
///
/// 编译:
///   cd modules/rust
///   cargo build --release
///   # 输出: target/release/librustmath.so
///
/// 或手动:
///   rustc --crate-type cdylib -o librustmath.so rustmath.rs

use std::ffi::{CStr, CString};
use std::os::raw::c_char;

// ── 函数定义 ──────────────────────────────────────────────────────

#[no_mangle]
pub extern "C" fn r_add(a: f64, b: f64) -> f64 {
    a + b
}

#[no_mangle]
pub extern "C" fn r_sub(a: f64, b: f64) -> f64 {
    a - b
}

#[no_mangle]
pub extern "C" fn r_mul(a: f64, b: f64) -> f64 {
    a * b
}

#[no_mangle]
pub extern "C" fn r_div(a: f64, b: f64) -> f64 {
    if b != 0.0 { a / b } else { 0.0 }
}

#[no_mangle]
pub extern "C" fn r_sqrt(x: f64) -> f64 {
    x.sqrt()
}

#[no_mangle]
pub extern "C" fn r_pow(base: f64, exp: f64) -> f64 {
    base.powf(exp)
}

#[no_mangle]
pub extern "C" fn r_sin(x: f64) -> f64 {
    x.sin()
}

#[no_mangle]
pub extern "C" fn r_cos(x: f64) -> f64 {
    x.cos()
}

#[no_mangle]
pub extern "C" fn r_abs(x: f64) -> f64 {
    x.abs()
}

#[no_mangle]
pub extern "C" fn r_factorial(n: i32) -> i32 {
    if n <= 1 { 1 } else { (2..=n).product() }
}

#[no_mangle]
pub extern "C" fn r_fibonacci(n: i32) -> i32 {
    if n <= 0 { return 0; }
    if n == 1 { return 1; }
    let (mut a, mut b) = (0, 1);
    for _ in 2..=n {
        let t = a + b;
        a = b;
        b = t;
    }
    b
}

#[no_mangle]
pub extern "C" fn r_is_prime(n: i32) -> bool {
    if n < 2 { return false; }
    if n < 4 { return true; }
    if n % 2 == 0 || n % 3 == 0 { return false; }
    let mut i = 5;
    while i * i <= n {
        if n % i == 0 || n % (i + 2) == 0 { return false; }
        i += 6;
    }
    true
}

#[no_mangle]
pub extern "C" fn r_version() -> *const c_char {
    // 泄漏 CString（模块生命周期内有效）
    let s = CString::new("rustmath 1.0.0 (Rust)").unwrap();
    s.into_raw()
}

// ── 模块注册（手动，因为 Rust 没有 C 宏） ─────────────────────────

#[repr(C)]
struct PB_FuncDef {
    name: *const c_char,
    signature: *const c_char,
    func_ptr: *const (),
}

static mut INFO_JSON: [u8; 4096] = [0; 4096];

#[no_mangle]
pub extern "C" fn pb_module_info() -> *const c_char {
    let json = r#"{"name":"rustmath","version":"1.0.0","language":"rust","functions":[{"name":"r_add","signature":"dd>d"},{"name":"r_sub","signature":"dd>d"},{"name":"r_mul","signature":"dd>d"},{"name":"r_div","signature":"dd>d"},{"name":"r_sqrt","signature":"d>d"},{"name":"r_pow","signature":"dd>d"},{"name":"r_sin","signature":"d>d"},{"name":"r_cos","signature":"d>d"},{"name":"r_abs","signature":"d>d"},{"name":"r_factorial","signature":"i>i"},{"name":"r_fibonacci","signature":"i>i"},{"name":"r_is_prime","signature":"i>b"},{"name":"r_version","signature":"v>s"}]}"#;
    unsafe {
        INFO_JSON[..json.len()].copy_from_slice(json.as_bytes());
        INFO_JSON[json.len()] = 0;
        INFO_JSON.as_ptr() as *const c_char
    }
}

#[no_mangle]
pub extern "C" fn pb_module_func(name: *const c_char) -> *const () {
    let name = unsafe { CStr::from_ptr(name) }.to_str().unwrap_or("");
    match name {
        "r_add"       => r_add as *const (),
        "r_sub"       => r_sub as *const (),
        "r_mul"       => r_mul as *const (),
        "r_div"       => r_div as *const (),
        "r_sqrt"      => r_sqrt as *const (),
        "r_pow"       => r_pow as *const (),
        "r_sin"       => r_sin as *const (),
        "r_cos"       => r_cos as *const (),
        "r_abs"       => r_abs as *const (),
        "r_factorial" => r_factorial as *const (),
        "r_fibonacci" => r_fibonacci as *const (),
        "r_is_prime"  => r_is_prime as *const (),
        "r_version"   => r_version as *const (),
        _ => std::ptr::null(),
    }
}
