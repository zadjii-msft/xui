# Tokenizer fixtures

## C# grammar

The test cache contains `csharp.tmLanguage` and `CSharp-LICENSE` from
<https://github.com/dotnet/csharp-tmLanguage>.

Pinned revision: `6317c3c9ba89d02b08fc69b52c345782d6a42ade`.

The upstream MIT license permits redistribution. `npm test` downloads both files
into `test/cache` and checks their SHA-256 hashes. These files supply the real
`source.cs` grammar for tokenizer tests. Neither Git nor the VSIX includes the cache.

## Control style catalog

`style-catalog.json` records Element-applicable schemas from XUI's generated compiler catalog.
Its `sourceCommit` identifies the source revision.
The extension README describes the synchronization command.
The snapshot supplies complete vocabulary coverage for tokenizer tests; it is not a second compiler schema.
Window-only Tooltip schemas are excluded.
The VSIX does not include this fixture.
