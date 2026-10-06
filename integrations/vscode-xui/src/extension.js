"use strict";

const service = require("./language-service");

function createProviders(vscode) {
  const cache = new WeakMap();
  const modelFor = (document, token) => {
    if (token.isCancellationRequested) return undefined;
    const cached = cache.get(document);
    if (cached?.version === document.version) return cached.model;
    const model = service.analyze(document.getText(), () => token.isCancellationRequested);
    if (model && !token.isCancellationRequested) cache.set(document, { version: document.version, model });
    return model;
  };
  const range = (document, start, end) => new vscode.Range(document.positionAt(start), document.positionAt(end));
  const documentation = (title, description) => {
    const markdown = new vscode.MarkdownString();
    markdown.appendCodeblock(title, "xui");
    if (description) markdown.appendText(`\n${description}`);
    return markdown;
  };
  return {
    provideCompletionItems(document, position, token) {
      const model = modelFor(document, token);
      if (!model) return [];
      return service.completions(model, document.offsetAt(position)).map((entry) => {
        const completion = new vscode.CompletionItem(entry.label, vscode.CompletionItemKind[entry.kind]);
        completion.detail = entry.detail;
        completion.documentation = documentation(entry.detail, entry.description);
        completion.range = range(document, entry.start, entry.end);
        completion.insertText = entry.snippet ? new vscode.SnippetString(entry.insertText) : entry.insertText;
        if (entry.kind === "Property" || entry.insertText.endsWith(" "))
          completion.command = { title: "Suggest values", command: "editor.action.triggerSuggest" };
        return completion;
      });
    },
    provideHover(document, position, token) {
      const model = modelFor(document, token);
      const entry = model && service.hover(model, document.offsetAt(position));
      return entry && new vscode.Hover(documentation(entry.title, entry.description), range(document, entry.start, entry.end));
    },
    provideSignatureHelp(document, position, token) {
      const model = modelFor(document, token);
      const entry = model && service.signature(model, document.offsetAt(position));
      if (!entry) return undefined;
      const help = new vscode.SignatureHelp();
      const signature = new vscode.SignatureInformation(entry.label, entry.description);
      signature.parameters = entry.parameters.map((p) => new vscode.ParameterInformation(p.label, p.description));
      help.signatures = [signature];
      help.activeSignature = 0;
      help.activeParameter = entry.activeParameter;
      return help;
    },
    provideDefinition(document, position, token) {
      const model = modelFor(document, token);
      const entry = model && service.definition(model, document.offsetAt(position));
      return entry && new vscode.Location(document.uri, range(document, entry.start, entry.nameEnd));
    },
    provideDocumentSymbols(document, token) {
      const model = modelFor(document, token);
      if (!model?.component) return [];
      const kinds = { component: "Class", style: "Class", resource: "Constant", param: "Property", state: "Variable", method: "Method", ref: "Field" };
      const symbol = (entry) => new vscode.DocumentSymbol(entry.name, entry.detail, vscode.SymbolKind[kinds[entry.kind]],
        range(document, entry.start, entry.end), range(document, entry.start, entry.nameEnd));
      const root = symbol(model.component);
      root.children = model.declarations.filter((entry) => entry !== model.component).map(symbol);
      return [root];
    }
  };
}

function activate(context) {
  const vscode = require("vscode");
  const providers = createProviders(vscode);
  const selector = { language: "xui" };
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(selector, providers, "(", ",", ":", ".", " "),
    vscode.languages.registerHoverProvider(selector, providers),
    vscode.languages.registerSignatureHelpProvider(selector, providers, "(", ",", ":"),
    vscode.languages.registerDefinitionProvider(selector, providers),
    vscode.languages.registerDocumentSymbolProvider(selector, providers)
  );
}

module.exports = { activate, createProviders };
