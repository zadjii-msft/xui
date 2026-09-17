using Xui.Designer;

var assertions = 0;
void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
    assertions++;
}

Require(DesignerTemplates.All.Count >= 7, "The template catalog has its expected examples.");
Require(DesignerTemplates.All.Select(template => template.Id).Distinct(StringComparer.Ordinal).Count() == DesignerTemplates.All.Count,
    "Template IDs are unique.");
foreach (var template in DesignerTemplates.All)
{
    Require(ReferenceEquals(DesignerTemplates.Get(template.Id), template), "Lookup returns the catalog entry.");
    Require(!string.IsNullOrWhiteSpace(template.Name) && !string.IsNullOrWhiteSpace(template.Description), "Templates have useful metadata.");
    var result = PreviewCompiler.Compile(template.Source);
    Require(result.Success, $"Template '{template.Name}' failed:\n{result.Diagnostics}");
    Require(string.IsNullOrWhiteSpace(result.Diagnostics), $"Template '{template.Name}' has diagnostics:\n{result.Diagnostics}");
    Console.WriteLine($"Compiled template: {template.Id}");
}
var values = DesignerTemplates.Get("values");
Require(values.Name == "Value controls", "The value-controls example has a discoverable catalog name.");
var parsed = Xui.Generator.XuiSourceParser.Parse(values.Source);
Require(parsed.Success, "The value-controls example uses the shared parser.");
var range = parsed.Root!.Children.Single(node => node.Kind == "RangeInput");
var progress = parsed.Root.Children.Single(node => node.Kind == "Progress");
Require(range.Arguments.Single(argument => argument.Name == "currentValue").Value == "Level" &&
    progress.Arguments.Single(argument => argument.Name == "currentValue").Value == "Level",
    "Both native value controls share the same component state.");
Require(range.Arguments.Single(argument => argument.Name == "change").Value == "SetLevel" &&
    values.Source.Contains("void SetLevel(double value)", StringComparison.Ordinal),
    "The range input binds a typed committed-change handler.");
foreach (string newline in new[] { "\r", "\r\n", "\n" })
{
    var result = PreviewCompiler.Compile(values.Source.ReplaceLineEndings(newline));
    Require(result.Success && string.IsNullOrWhiteSpace(result.Diagnostics), "The value template compiles with each native/source newline shape.");
}
bool rejected = false;
try { DesignerTemplates.Get("missing"); }
catch (ArgumentException) { rejected = true; }
Require(rejected, "Unknown template IDs fail explicitly.");
Console.WriteLine($"Designer template assertions: {assertions} passed.");
