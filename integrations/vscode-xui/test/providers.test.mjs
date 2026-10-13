import assert from "node:assert/strict";
import { createRequire } from "node:module";
import test from "node:test";

const { createProviders } = createRequire(import.meta.url)("../src/extension");
class Range { constructor(start, end) { Object.assign(this, { start, end }); } }
class MarkdownString {
  value = "";
  appendCodeblock(value) { this.value += value; return this; }
  appendText(value) { this.value += value; return this; }
}
class CompletionItem { constructor(label, kind) { Object.assign(this, { label, kind }); } }
class SnippetString { constructor(value) { this.value = value; } }
class Hover { constructor(contents, range) { Object.assign(this, { contents, range }); } }
class SignatureInformation { constructor(label, documentation) { Object.assign(this, { label, documentation }); } }
class Location { constructor(uri, range) { Object.assign(this, { uri, range }); } }
class DocumentSymbol {
  constructor(name, detail, kind, range, selectionRange) {
    Object.assign(this, { name, detail, kind, range, selectionRange, children: [] });
  }
}
const vscode = { Range, MarkdownString, CompletionItem, SnippetString, Hover, SignatureInformation,
  SignatureHelp: class {}, ParameterInformation: SignatureInformation, Location, DocumentSymbol,
  CompletionItemKind: { Constructor: 4, Property: 9 }, SymbolKind: { Class: 4, Variable: 12 } };
const token = { isCancellationRequested: false };
function document(text) {
  return { text, version: 1, reads: 0, uri: "untitled:example.xui", getText() { ++this.reads; return this.text; },
    positionAt(offset) { return offset; }, offsetAt(position) { return position; } };
}

test("provider adapter maps snippets, ranges and follow-up value suggestions", () => {
  const provider = createProviders(vscode);
  const doc = document('component C { view { VStack() { But } } }');
  const item = provider.provideCompletionItems(doc, doc.text.indexOf("But") + 3, token)[0];
  assert.equal(item.label, "Button");
  assert.ok(item.insertText instanceof SnippetString);
  assert.equal(item.range.end - item.range.start, 3);
  assert.equal(item.documentation.isTrusted, undefined);
  doc.text = 'component C { view { Button("x", cli); } }';
  ++doc.version;
  const prop = provider.provideCompletionItems(doc, doc.text.indexOf("cli") + 3, token)[0];
  assert.equal(prop.insertText, "click: ");
  assert.equal(prop.command.command, "editor.action.triggerSuggest");
});

test("providers share versioned models and return no results on cancellation", () => {
  const provider = createProviders(vscode);
  const doc = document('component C { view { VStack() { But } } }');
  const position = doc.text.indexOf("But") + 3;
  provider.provideCompletionItems(doc, position, token);
  provider.provideHover(doc, position, token);
  provider.provideDocumentSymbols(doc, token);
  assert.equal(doc.reads, 1);
  ++doc.version;
  provider.provideCompletionItems(doc, position, token);
  assert.equal(doc.reads, 2);
  const cancelled = { isCancellationRequested: true };
  assert.deepEqual(provider.provideCompletionItems(doc, position, cancelled), []);
  assert.equal(provider.provideHover(doc, position, cancelled), undefined);
  assert.equal(provider.provideSignatureHelp(doc, position, cancelled), undefined);
  assert.equal(provider.provideDefinition(doc, position, cancelled), undefined);
  assert.deepEqual(provider.provideDocumentSymbols(doc, cancelled), []);
  assert.equal(doc.reads, 2);
});

test("provider adapter exposes signatures, local definitions and component outlines", () => {
  const provider = createProviders(vscode);
  const doc = document('component C { state string Title = "Hi"; view { Button(Title, id: "ok"); } }');
  const definition = provider.provideDefinition(doc, doc.text.indexOf("Button(Title") + 8, token);
  assert.equal(definition.uri, doc.uri);
  assert.equal(doc.text.slice(definition.range.start, definition.range.end), "Title");
  const help = provider.provideSignatureHelp(doc, doc.text.indexOf('id:') + 4, token);
  assert.equal(help.signatures[0].parameters[help.activeParameter].label, "id: string");
  const outline = provider.provideDocumentSymbols(doc, token);
  assert.equal(outline[0].name, "C");
  assert.equal(outline[0].children[0].name, "Title");
  assert.ok(outline[0].range.end >= outline[0].children[0].range.end);
});
