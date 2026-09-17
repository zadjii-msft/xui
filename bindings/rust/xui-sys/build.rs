fn main() {
    println!("cargo:rerun-if-env-changed=XUI_LIB_DIR");
    let target = std::env::var("TARGET").expect("Cargo must set TARGET");
    let architecture = match target.as_str() {
        "x86_64-pc-windows-msvc" => "x64",
        "aarch64-pc-windows-msvc" => "ARM64",
        _ => panic!(
            "XUI does not support target {target}; use x86_64-pc-windows-msvc or aarch64-pc-windows-msvc"
        ),
    };
    let root = std::path::PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let lib = std::env::var_os("XUI_LIB_DIR")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| {
            let bundled = root.join("native").join(&target);
            // Only an actual source checkout can use the native development build.
            let checkout = root.ancestors().nth(3);
            if !root.join("native").exists()
                && let Some(checkout) = checkout
                && checkout.join("bindings").join("rust").join("xui-sys") == root
                && checkout.join("CMakeLists.txt").is_file()
            {
                checkout.join("build").join(architecture).join("Release")
            } else {
                bundled
            }
        });
    for name in ["xui.lib", "xui.dll"] {
        let asset = lib.join(name);
        assert!(
            asset.is_file(),
            "Missing XUI native asset for {target}: {}. Use a release crate containing native/{target}, \
             build the checkout's build/{architecture}/Release native targets, or set XUI_LIB_DIR \
             to a directory containing matching xui.lib and xui.dll.",
            asset.display()
        );
        println!("cargo:rerun-if-changed={}", asset.display());
    }
    let lib = lib
        .canonicalize()
        .expect("Cannot resolve the XUI native library directory");
    println!("cargo:rustc-link-search=native={}", lib.display());
    println!("cargo:rustc-link-lib=dylib=xui");
    println!("cargo:runtime_dir={}", lib.display());
    println!("cargo:runtime_dll={}", lib.join("xui.dll").display());
}
