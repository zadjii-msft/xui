"use strict";

const data = require("../data/language.json");
const controls = data.controls;
const targetName = (name) => Object.hasOwn(data.aliases, name ?? "") ? data.aliases[name] : name;
const schemas = new Map(data.schemas.map((schema) => [`${schema.target}.${schema.part}`, schema]));
const schemaFor = (target, part = "root") => schemas.get(`${targetName(target)}.${part}`);
const containers = new Set(["VStack", "HStack", "Grid", "ScrollView", "Popup", "SplitView", "Reveal"]);
const isStack = (name) => name === "VStack" || name === "HStack";
const controlDescriptions = {
  VStack: "Lays out children vertically.", HStack: "Lays out children horizontally.",
  Grid: "Lays out children in rows and columns. Track arrays default to one star track.",
  Text: "Displays text and uses that text as its accessible name.",
  Button: "Invokes a named C# method when clicked.",
  Toggle: "Binary choice with a bool change handler.", ToggleSwitch: "Binary choice presented as a switch.",
  ToggleButton: "Toggle button. Its bool change event does not also emit click.",
  CheckBox: "Check box with a CheckState change handler and optional three-state input.",
  HyperlinkButton: "Link presentation with an application-owned click action; does not navigate automatically.",
  SelectorBar: "Single selection from a Choice array. The change handler receives a ulong ID.",
  InfoBadge: "Noninteractive badge. Use count or icon, never both; omit both for a dot.",
  MenuBar: "Menu bar with Command arrays and ulong invoke/pin handlers.",
  TextInput: "Native text input with separate accessible name and text value.",
  DataGrid: "Native data grid with GridColumn definitions.", NavigationView: "Native navigation and search control.",
  ItemsView: "Native item collection.", ScrollView: "Scrollable container with exactly one child.",
  Popup: "Popup container with exactly one child.", SplitView: "Split container with exactly two children.",
  Reveal: "Animated clipping container with exactly one child. Duration defaults to zero.",
  RangeInput: "Native slider with a committed double change handler.",
  Progress: "Read-only linear progress.", ProgressRing: "Circular progress; indeterminate by default.",
  SwapChainPanel: "Windows-only native graphics leaf.",
  Content: "Embeds an existing Element. Its expression runs once and cannot depend on state."
};
const details = {
  ref: ["identifier", "Creates a typed public element reference. The name must be unique and cannot start with __xui."],
  size: ["(float Width, float Height)", "Fixed size in DIPs; this is not a scalar style metric."],
  preferredSize: ["(float Width, float Height)", "Preferred size in DIPs."],
  id: ["string", "Native automation ID."], help: ["string", "Native accessibility help text."],
  name: ["string", "Accessible name, separate from the TextInput text value."],
  enabled: ["bool", "Whether the control is enabled."], visible: ["bool", "Whether the control is visible."],
  text: ["string", "Native input text."], placeholder: ["string", "Text shown when the input is empty."],
  checked: ["bool", "Checked state. Setting it does not raise change."],
  checkState: ["CheckState", "Unchecked, Checked, or Indeterminate."],
  threeState: ["bool", "Enables three-state input; defaults to false."],
  captionVisible: ["bool", "Whether the native input caption is shown."],
  headerVisible: ["bool", "Whether the navigation header is shown."],
  secondVisible: ["bool", "Whether the secondary split pane is shown."],
  windowBackground: ["bool", "Whether the popup uses the window background."],
  searchId: ["string", "Automation ID of the native navigation search input."],
  searchHelp: ["string", "Help text of the native navigation search input."],
  icon: ["ButtonIcon", "Native button icon."], count: ["uint", "Badge count; mutually exclusive with icon."],
  items: ["global::Xui.Choice[]", "Selector choices; applied together with selection."],
  selected: ["ulong?", "Selected choice ID, or null."], commands: ["global::Xui.Command[]", "Root commands must be submenus."],
  range: ["global::Xui.NumericRange", "Construction-only bounds and steps. Cannot depend on state or component methods."],
  currentValue: ["double", "Finite value inside the range. Use currentValue, not value:."],
  orientation: ["Axis", "Slider orientation."], reversed: ["bool", "Reverses the slider direction."],
  progressState: ["ProgressState", "Progress presentation state."], open: ["bool", "Whether the retained child is revealed."],
  duration: ["uint", "Animation duration from 0 through 10000 milliseconds; zero disables motion."],
  layout: ["RevealLayout", "Fixed or expanding reveal layout."], direction: ["RevealDirection", "Reveal entry edge."],
  placement: ["PopupPlacement", "Popup placement relative to its anchor."],
  row: ["int", "Zero-based Grid row; fixed for the component lifetime."],
  column: ["int", "Zero-based Grid column; fixed for the component lifetime."],
  rowSpan: ["int", "Positive Grid row span; defaults to one."],
  columnSpan: ["int", "Positive Grid column span; defaults to one."],
  flex: ["float", "Stack flex weight; fixed for the component lifetime."],
  style: ["style identifier", "A named style declared in this component with the matching target."]
};
const colors = new Set(["background", "foreground", "borderBrush"]);
const events = new Set(["click", "change", "submit", "invoke", "pin"]);
const hasControl = (name) => Object.hasOwn(controls, name ?? "");
const hasEnum = (name) => Object.hasOwn(data.enums, name ?? "");

function propertyInfo(name, control, schema, inStyle = false) {
  if (!inStyle && events.has(name)) {
    const parameter = name === "change" ? ({
      Toggle: "bool", ToggleSwitch: "bool", ToggleButton: "bool", CheckBox: "global::Xui.CheckState",
      SelectorBar: "ulong", RangeInput: "double", TextInput: "string"
    }[control] ?? "") : ["invoke", "pin"].includes(name) ? "ulong" : "";
    return { type: `void handler(${parameter})`, description: "Name of a C# method in code csharp.", handler: true };
  }
  if (!inStyle && Object.hasOwn(details, name)) return { type: details[name][0], description: details[name][1] };
  if (!inStyle && ["rows", "columns"].includes(name))
    return { type: control === "DataGrid" ? "global::Xui.GridColumn[]" : "global::Xui.GridTrack[]", description: "Column or track definitions." };
  if (!inStyle && isStack(control) && ["spacing", "padding"].includes(name))
    return { type: "float", description: "Structural DIP value; overrides the named style." };
  if (colors.has(name)) return { type: "RGB24 color", description: "RGB24 integer, resource(Name), or theme(light: RGB24, dark: RGB24).", color: true };
  if (["padding", "borderThickness"].includes(name))
    return { type: "insets", description: "One dimension or (left, top, right, bottom); 0 through 32768 DIPs." };
  const limits = schema?.limits ?? [32768, 1024, 7, 15, 15];
  if (name === "fontStyle" || name.endsWith("Alignment")) {
    const options = name === "fontStyle" ? ["normal", "italic", "oblique"] : ["start", "center", "end", "stretch"];
    const mask = limits[name === "fontStyle" ? 2 : name === "horizontalAlignment" ? 3 : 4];
    return { type: "style enum", description: "Unquoted style literal.", values: options.filter((_, i) => mask & (1 << i)) };
  }
  if (name === "wrapping") return { type: "bool", description: "Constant text-wrapping flag.", values: ["true", "false"] };
  if (name === "fontFamily") return { type: "string literal", description: `Nonempty font family; at most ${limits[1]} UTF-16 code units and 1024 UTF-8 bytes.` };
  if (name === "fontSize") return { type: "positive dimension", description: `Positive font size, at most ${limits[0]} DIPs.` };
  if (name === "fontWeight") return { type: "integer literal", description: "Font weight from 1 through 999." };
  return { type: "dimension literal", description: `${name === "rowHeight" ? "Positive value" : "Value from 0"} through 32768 DIPs; style values must be constants.` };
}

function argumentsFor(control, parent, styleTarget) {
  const names = new Set((hasControl(control) ? controls[control] : []).filter((name) => name !== "value"));
  const schema = schemaFor(control === "Content" ? styleTarget : control);
  if (schema || control === "Content") names.add("style");
  for (const name of schema?.properties ?? []) names.add(name);
  for (const name of ["size", "preferredSize", "ref"]) names.add(name);
  if (parent === "Grid") for (const name of ["row", "column", "rowSpan", "columnSpan"]) names.add(name);
  if (isStack(parent)) names.add("flex");
  if (!isStack(control) && !["Grid", "Content"].includes(control))
    for (const name of ["id", "enabled", "visible", "help"]) names.add(name);
  return [...names];
}

module.exports = { ...data, controls, targetName, schemaFor, containers, isStack, controlDescriptions, propertyInfo, argumentsFor, colors, events, hasControl, hasEnum };
