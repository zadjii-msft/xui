fn main() {
    let root = std::path::PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let lib = std::env::var_os("XUI_LIB_DIR")
        .map(std::path::PathBuf::from)
        .unwrap_or_else(|| {
            root.join("..")
                .join("..")
                .join("..")
                .join("build")
                .join("arm64")
                .join("Release")
        });
    println!("cargo:rustc-link-search=native={}", lib.display());
    println!("cargo:rustc-link-lib=dylib=xui");
    println!("cargo:rerun-if-env-changed=XUI_LIB_DIR");
}
