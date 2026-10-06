"use strict";

const assert = require("node:assert/strict");
const { writeFile, mkdir } = require("node:fs/promises");
const path = require("node:path");
const vscode = require("vscode");

async function run() {
  const result = path.join(__dirname, "cache", "editor-result.json");
  await mkdir(path.dirname(result), { recursive: true });
  await writeFile(result, JSON.stringify({ status: "running" }));
  try {
    const extension = vscode.extensions.getExtension("zadjii-msft.xui");
    assert.ok(extension, "The XUI development extension must be installed");
    const document = await vscode.workspace.openTextDocument({ language: "xui", content: `component Example {
  state string Title = "Hello";
  style Heading for Label { fontSize: 20; }
  view {
    VStack() {
      But
      Text(Title, style: Heading);
    }
  }
}` });
    const editor = await vscode.window.showTextDocument(document);
    const point = (text, delta = text.length) => document.positionAt(document.getText().indexOf(text) + delta);
    const complete = (position) => vscode.commands.executeCommand("vscode.executeCompletionItemProvider", document.uri, position);
    let position = point("But");
    let list = await complete(position);
    const activationDeadline = Date.now() + 5000;
    while (!list.items.some((item) => item.label === "Button") && Date.now() < activationDeadline) {
      await new Promise((resolve) => setTimeout(resolve, 50));
      list = await complete(position);
    }
    assert.equal(extension.isActive, true, "Opening an XUI document must activate IntelliSense automatically");
    const button = list.items.find((item) => item.label === "Button");
    assert.ok(button, "Registered provider must return a Button completion");
    assert.ok(button.insertText instanceof vscode.SnippetString);
    await editor.insertSnippet(button.insertText, button.range);
    assert.ok(document.getText().includes('Button("Button");'), "The actual snippet must insert a valid leaf");
    await vscode.commands.executeCommand("leaveSnippet");

    const valueEnd = point('Button("Button"');
    await editor.edit((edit) => edit.insert(valueEnd, ", cli"));
    position = point(", cli");
    list = await complete(position);
    const click = list.items.find((item) => item.label === "click");
    assert.ok(click);
    assert.equal(click.insertText, "click: ");
    await editor.edit((edit) => edit.replace(click.range, click.insertText + "Save"));
    assert.ok(document.getText().includes('Button("Button", click: Save);'));

    const signatures = await vscode.commands.executeCommand("vscode.executeSignatureHelpProvider", document.uri, point("click: "));
    assert.match(signatures.signatures[0].parameters[signatures.activeParameter].label, /^click:/);
    const hovers = await vscode.commands.executeCommand("vscode.executeHoverProvider", document.uri, point("click", 2));
    assert.ok(hovers.some((hover) => hover.contents.some((content) => content.value.includes("handler"))));
    for (const [use, name] of [["Text(Title", "Title"], ["style: Heading", "Heading"]]) {
      const definitions = await vscode.commands.executeCommand("vscode.executeDefinitionProvider", document.uri, point(use, use.length - 1));
      assert.ok(definitions.some((definition) => document.getText(definition.range ?? definition.targetSelectionRange) === name));
    }
    const symbols = await vscode.commands.executeCommand("vscode.executeDocumentSymbolProvider", document.uri);
    assert.ok(symbols.some((symbol) => symbol.name === "Example" && symbol.children.some((child) => child.name === "Title")));

    const content = 'component C { view { CheckBox("Check", checkState: ); } }';
    await editor.edit((edit) => edit.replace(new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), content));
    list = await complete(point("checkState: "));
    const checked = list.items.find((item) => item.label === "Checked");
    assert.equal(checked?.insertText, "global::Xui.CheckState.Checked", "Edits must invalidate the cached parse");

    const trailingComma = `component Counter {
  state int Count = 0;
  view {
    VStack(spacing: 8, padding: 16) {
      Button("Increment", click: Increment, id: "increment", );
    }
  }
  code csharp { void Increment() => Count++; }
}`;
    await editor.edit((edit) => edit.replace(new vscode.Range(document.positionAt(0), document.positionAt(document.getText().length)), trailingComma));
    for (const suffix of ['id: "increment",', 'id: "increment", ']) {
      position = point(suffix);
      editor.selection = new vscode.Selection(position, position);
      list = await complete(position);
      const properties = list.items.filter((item) => item.kind === vscode.CompletionItemKind.Property);
      for (const name of ["enabled", "visible", "help", "size", "icon", "style"])
        assert.ok(properties.some((item) => item.label === name), `${name} must be suggested after the trailing comma`);
      for (const name of ["click", "id"])
        assert.ok(!properties.some((item) => item.label === name), `${name} must not be duplicated`);
      const enabled = properties.find((item) => item.label === "enabled");
      assert.equal(enabled.range.isEmpty, true, "An empty argument has an empty replacement range");
      assert.ok(enabled.range.start.isEqual(position));
    }
    const enabled = list.items.find((item) => item.label === "enabled");
    await editor.edit((edit) => edit.replace(enabled.range, enabled.insertText + "true"));
    assert.ok(document.getText().includes('id: "increment", enabled: true);'));
    await writeFile(result, JSON.stringify({ status: "passed", vscode: vscode.version }, null, 2));
  } catch (error) {
    await writeFile(result, JSON.stringify({ status: "failed", message: error.stack }, null, 2));
    throw error;
  } finally {
    await vscode.commands.executeCommand("workbench.action.revertAndCloseActiveEditor");
  }
}

module.exports = { run };
