# Choose a language

XUI has one native Windows engine for layout, drawing, input, and accessibility.
C++, C, C#, and Rust applications use that engine.
The `.xui` compiler generates C# that uses the managed binding.

## Support comparison

| Path | Application model | Control access | Important boundary |
| --- | --- | --- | --- |
| [C++20](cpp.md) | Native objects, `std::shared_ptr`, and callbacks | Public C++ headers | Broadest API, including native worker helpers and advanced providers |
| [C ABI](c.md) | Opaque handles, records, and status-returning callbacks | Public C headers, including feature and layout extensions | No C++ class ABI, automatic dispatcher, or independent control destruction |
| [Handwritten C#](csharp.md) | Window factories, properties, and events | Managed wrappers over the C ABI | Family coverage does not imply every C++ method exists |
| [Rust](rust.md) | Window factories, `Result`, and closures | Safe `xui` wrappers over `xui-sys` | Controls retain an `Rc` owner and are neither `Send` nor `Sync` |
| [Declarative `.xui` and C#](declarative.md) | A fixed control tree with C# state and handlers | Fourteen node forms, including `Content` | No Rust or C++ generator, runtime markup parser, or arbitrary dynamic children |

The feature extension adds typed factories to the C# and Rust baseline.
Handwritten extensions also supply selected navigation, text, and retained-child APIs.
The [binding coverage table](../bindings.md#current-coverage) and [advanced API gaps](../bindings.md#advanced-api-gaps) define the limits.

The [control catalog](../controls/README.md) describes control behavior.
Its C++ examples do not establish binding parity.
The public headers and wrapper declarations define the callable API for each language.

## Shared behavior

- Windows hosts the native controls. XUI is not a cross-platform GUI backend.
- Sizes use device-independent pixels (DIPs).
- Each control has one layout parent.
- Construction finishes before the window runs.
- UI access and callbacks belong to the creating UI thread.
- Native text fields retain Windows editing, selection, IME, and accessibility.
- Large collection controls use source snapshots rather than one child control per row.
- Optional WebView2 requires separate build support and an installed runtime.

The exported style catalog has 46 targets and 223 target/part schemas.
These numbers describe presentation schemas, not constructors or complete language parity.
Shared styles do not provide arbitrary control templates, row templates, or Shell `HMENU` styling.
The [style contract](../control-styling.md) and [inventory](../control-styling-inventory.md) describe exact support.

## Choose a starting point

- For direct access to the native API, use [C++20](cpp.md).
- For an existing C application or another foreign-function interface, use the [C ABI](c.md).
- For explicit managed composition, use [handwritten C#](csharp.md).
- For an existing Rust application, use the [Rust wrappers](rust.md).
- For a fixed interface with C# state, use [declarative XUI](declarative.md).

## Build and deployment references

Build and test procedures belong in [CONTRIBUTING](../../../CONTRIBUTING.md).
The language pages contain application source and API guidance.

| Task | Procedure |
| --- | --- |
| Native requirements and compilation | [Build the native code](../../../CONTRIBUTING.md#build-the-native-code) |
| C++ project integration | [Use XUI in a C++ application](../../../CONTRIBUTING.md#use-xui-in-a-c-application) |
| C project integration | [C ABI application setup](../../../CONTRIBUTING.md#c-abi-application-setup) |
| Managed samples and development reload | [C# and declarative samples](../../../CONTRIBUTING.md#c-and-declarative-samples) |
| Rust compilation and checks | [Rust](../../../CONTRIBUTING.md#rust) |
| NativeAOT and deployment | [NativeAOT and deployment](../../../CONTRIBUTING.md#nativeaot-and-deployment) |
| Regression checks | [Tests](../../../CONTRIBUTING.md#tests) |

## Next

- [Tutorials](../tutorials/README.md)
- [Controls](../controls/README.md)
- [Application lifecycle](../application.md)
- [C ABI and binding contracts](../bindings.md)
- [Declarative language contract](../xui-language.md)
