fn main() {
    let root = std::path::PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let manifest = root
        .join("..")
        .join("..")
        .join("..")
        .join("demo")
        .join("xui.manifest");
    println!("cargo:rustc-link-arg=/MANIFEST:EMBED");
    println!("cargo:rustc-link-arg=/MANIFESTINPUT:{}", manifest.display());
    println!("cargo:rerun-if-changed={}", manifest.display());
}
