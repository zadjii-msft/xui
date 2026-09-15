# Repository guidance

Keep this repository application-author first.

- Keep `README.md` brief, with usable examples and links.
- Put build, test, and contributor procedures in `CONTRIBUTING.md`.
- Put public contracts, guides, and design proposals in `docs/specs`.
- Put implementation notes, handoffs, and historical evidence in `docs/llm`.
- When you add a document, update the relevant index.

Read [the maintainer index](docs/llm/README.md) for source maps and previous investigations.
Read [the public references](docs/specs/README.md) before changing behavior.
Inspect current source before treating a historical handoff as current state.
Preserve native text input, accessibility, ownership, cancellation, and explicit error behavior.
Keep dated measurements and test results out of the root README.
