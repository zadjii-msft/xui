using Xui.Designer;

var assertions = 0;
void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
    assertions++;
}

Require(DesignerTemplates.All.Count >= 6, "The template catalog has its expected examples.");
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
bool rejected = false;
try { DesignerTemplates.Get("missing"); }
catch (ArgumentException) { rejected = true; }
Require(rejected, "Unknown template IDs fail explicitly.");
Console.WriteLine($"Designer template assertions: {assertions} passed.");
