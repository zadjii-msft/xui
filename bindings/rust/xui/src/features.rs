use super::*;

pub(crate) fn value_record() -> sys::FeatureValue {
    sys::FeatureValue {
        size: size_of::<sys::FeatureValue>() as u32,
        version: 0x10001,
        ..Default::default()
    }
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct NumericRange {
    pub minimum: f64,
    pub maximum: f64,
    pub small_step: f64,
    pub large_step: f64,
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TextSelection {
    pub start: u64,
    pub end: u64,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ItemKey {
    pub id: u64,
    pub version: u64,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SelectionInfo {
    pub focused: Option<ItemKey>,
    pub storage_terms: u32,
}
#[derive(Clone, Debug)]
pub struct Choice {
    pub id: u64,
    pub text: String,
    pub enabled: bool,
    pub version: u64,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RgbaColor {
    pub red: u8,
    pub green: u8,
    pub blue: u8,
    pub alpha: u8,
}
impl RgbaColor {
    pub(crate) fn packed(self) -> u64 {
        u32::from_le_bytes([self.red, self.green, self.blue, self.alpha]) as u64
    }
    pub(crate) fn from_packed(v: u64) -> Self {
        let [red, green, blue, alpha] = (v as u32).to_le_bytes();
        Self {
            red,
            green,
            blue,
            alpha,
        }
    }
}
macro_rules! enums {
    ($($name:ident {$($variant:ident = $value:expr),*}),* $(,)?) => {$(
        #[derive(Clone, Copy, Debug, PartialEq, Eq)]
        #[repr(u32)]
        pub enum $name { $($variant = $value),* }
    )*};
}
enums!(
    ProgressState {Determinate=0,Indeterminate=1,Paused=2,Error=3,Unknown=4},
    ItemsPresentation {List=0,Tiles=1,Grouped=2},
    CompactNavigation {Stacked=0,Overlay=1},
    DateTimePresentation {Date=0,Time=1,Calendar=2},
    TextCommand {Undo=0,Redo=1,Copy=2,Cut=3,Paste=4,SelectAll=5},
    StatusSeverity {Information=0,Success=1,Warning=2,Error=3},
    HostState {Idle=0,Loading=1,Ready=2,Playing=3,Paused=4,Stopped=5,Suspended=6,Error=7},
    ButtonBehavior {Momentary=0,Repeat=1,Toggle=2,Dropdown=3},
    ButtonIcon {None=0,Back=1,Forward=2,Up=3,Refresh=4,Split=5,Theme=6,Add=7,
        Minimize=8,Maximize=9,Restore=10,Close=11,More=12,Navigation=13,Home=14,
        Folder=15,Settings=16,Search=17,Library=18,History=19,Bookmark=20,Drive=21,Open=22},
    TrackSizing {Fixed=0,Automatic=1,Star=2},
    CommandKind {Action=0,Submenu=1,Separator=2}
);
impl ProgressState {
    pub(crate) fn from_native(v: u64) -> Result<Self> {
        match v {
            0 => Ok(Self::Determinate),
            1 => Ok(Self::Indeterminate),
            2 => Ok(Self::Paused),
            3 => Ok(Self::Error),
            4 => Ok(Self::Unknown),
            _ => Err(invalid("Invalid progress state.")),
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct LocalDateTime {
    pub year: u32,
    pub month: u32,
    pub day: u32,
    pub hour: u32,
    pub minute: u32,
    pub second: u32,
}
#[derive(Clone, Debug)]
pub struct TextRun {
    pub text: String,
    pub bold: bool,
    pub italic: bool,
    pub underline: bool,
    pub link: String,
}
#[derive(Clone, Copy)]
pub struct GridTrack {
    pub sizing: TrackSizing,
    pub value: f32,
    pub minimum: f32,
    pub maximum: f32,
}
impl Default for GridTrack {
    fn default() -> Self {
        Self {
            sizing: TrackSizing::Star,
            value: 1.,
            minimum: 0.,
            maximum: f32::MAX,
        }
    }
}
#[derive(Clone, Debug)]
pub struct GridColumn {
    pub name: String,
    pub width: f32,
    pub numeric: bool,
    pub filterable: bool,
    pub checkable: bool,
}
#[derive(Clone, Debug)]
pub struct Command {
    pub id: u64,
    pub parent: u64,
    pub label: String,
    pub kind: CommandKind,
    pub enabled: bool,
    pub checked: Option<bool>,
    pub shortcut_hint: String,
    pub pin_label: String,
}
#[derive(Clone, Copy, Default)]
pub struct KeyModifiers {
    pub control: bool,
    pub shift: bool,
    pub alt: bool,
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct GeoPoint {
    pub latitude: f64,
    pub longitude: f64,
}
#[derive(Clone, Debug)]
pub struct MapMarker {
    pub id: u64,
    pub location: GeoPoint,
    pub name: String,
}
#[derive(Clone, Copy, Default)]
pub struct ScenePoint {
    pub x: f32,
    pub y: f32,
}
#[derive(Clone, Copy, Default)]
pub struct SceneColor {
    pub red: f32,
    pub green: f32,
    pub blue: f32,
    pub alpha: f32,
}
#[derive(Clone, Copy)]
pub struct SceneTransform {
    pub m11: f64,
    pub m12: f64,
    pub m21: f64,
    pub m22: f64,
    pub dx: f64,
    pub dy: f64,
}
impl Default for SceneTransform {
    fn default() -> Self {
        Self {
            m11: 1.,
            m12: 0.,
            m21: 0.,
            m22: 1.,
            dx: 0.,
            dy: 0.,
        }
    }
}
#[derive(Clone, Copy, Default)]
pub struct SceneClip {
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
}
#[derive(Clone, Default)]
pub struct VectorShape {
    pub id: u64,
    pub points: Vec<ScenePoint>,
    pub name: String,
    pub closed: bool,
    pub interactive: bool,
    pub fill: SceneColor,
    pub stroke: SceneColor,
    pub stroke_width: f32,
    pub transform: SceneTransform,
    pub clip: Option<SceneClip>,
}
impl Window {
    pub fn web_content_enabled() -> bool {
        unsafe { sys::xui_capabilities() & 1 != 0 }
    }
    pub(crate) fn feature_create(
        &self,
        kind: u32,
        name: &str,
        content: Option<&Element>,
        second: Option<&Element>,
        mode: u32,
    ) -> Result<Element> {
        if let Some(c) = content {
            c.belongs(&self.0)?;
        }
        if let Some(c) = second {
            c.belongs(&self.0)?;
        }
        let version = unsafe { sys::xui_feature_version() };
        if version >> 16 != 1 || version < 0x10001 {
            return Err(invalid("Feature ABI is unavailable."));
        }
        let options = sys::FeatureOptions {
            size: size_of::<sys::FeatureOptions>() as u32,
            version: 0x10001,
            name: text(name)?,
            content: content.map_or(0, |v| v.handle),
            second: second.map_or(0, |v| v.handle),
            mode,
            reserved: 0,
        };
        let mut handle = 0;
        check(unsafe { sys::xui_feature_create(self.0.handle, kind, &options, &mut handle) })?;
        Ok(Element {
            owner: self.0.clone(),
            handle,
        })
    }
    pub fn show_shell_commands(&self, anchor: &Element, paths: &[&str]) -> Result<()> {
        anchor.belongs(&self.0)?;
        if paths.len() > 256 {
            return Err(invalid("Shell selection exceeds 256 paths."));
        }
        let values = paths.iter().map(|s| text(s)).collect::<Result<Vec<_>>>()?;
        check(unsafe { sys::xui_shell_show(anchor.handle, values.as_ptr(), values.len() as u32) })
    }
    pub fn with_titlebar(title: &str, width: f32, height: f32) -> Result<Self> {
        let options = sys::Options {
            size: size_of::<sys::Options>() as u32,
            version: sys::ABI_VERSION,
            title: text(title)?,
            width,
            height,
            ..Default::default()
        };
        let mut handle = 0;
        check(unsafe { sys::xui_window_create_features(&options, 1, &mut handle) })?;
        Ok(Self(Rc::new(Inner {
            handle,
            subscriptions: RefCell::new(HashMap::new()),
            callback_error: RefCell::new(None),
        })))
    }
    fn titlebar_child(&self, index: u32) -> Result<Element> {
        let mut handle = 0;
        self.0.check(unsafe { sys::xui_feature_child(self.0.handle, index, &mut handle) })?;
        Ok(Element { owner: self.0.clone(), handle })
    }
    pub fn titlebar_tabs(&self) -> Result<TabStrip> { self.titlebar_child(0).map(TabStrip) }
    pub fn titlebar_leading(&self) -> Result<Button> { self.titlebar_child(1).map(Button) }
    pub fn titlebar_secondary_tabs(&self) -> Result<TabStrip> { self.titlebar_child(2).map(TabStrip) }
    pub fn titlebar(&self) -> Result<Element> { self.titlebar_child(3) }
    pub fn titlebar_title(&self) -> Result<Label> { self.titlebar_child(4).map(Label) }
    pub fn titlebar_minimize(&self) -> Result<Button> { self.titlebar_child(5).map(Button) }
    pub fn titlebar_maximize(&self) -> Result<Button> { self.titlebar_child(6).map(Button) }
    pub fn titlebar_close(&self) -> Result<Button> { self.titlebar_child(7).map(Button) }
}
impl Element {
    fn collection_selection(&self) -> Result<SelectionInfo> {
        let v = self.feature_get(41)?;
        Ok(SelectionInfo {
            focused: (v.a != 0.).then_some(ItemKey {
                id: v.first,
                version: v.second,
            }),
            storage_terms: v.b as u32,
        })
    }
    fn collection_contains(&self, key: ItemKey) -> Result<bool> {
        let mut selected = 0;
        self.owner.check(unsafe {
            sys::xui_collection_contains(self.handle, key.id, key.version, &mut selected)
        })?;
        Ok(selected != 0)
    }
    pub(crate) fn feature_set(&self, property: u32, value: sys::FeatureValue) -> Result<()> {
        self.owner
            .check(unsafe { sys::xui_feature_set(self.handle, property, &value) })
    }
    pub(crate) fn feature_get(&self, property: u32) -> Result<sys::FeatureValue> {
        let mut v = value_record();
        self.owner
            .check(unsafe { sys::xui_feature_get(self.handle, property, &mut v) })?;
        Ok(v)
    }
    pub(crate) fn feature_action(&self, action: u32, first: u64, second: u64) -> Result<()> {
        self.owner
            .check(unsafe { sys::xui_feature_action(self.handle, action, first, second) })
    }
    pub(crate) fn feature_child(&self, index: u32) -> Result<Element> {
        self.optional_feature_child(index)?
            .ok_or_else(|| invalid("The retained child is unavailable."))
    }
    fn optional_feature_child(&self, index: u32) -> Result<Option<Element>> {
        let mut handle = 0;
        self.owner.check(unsafe { sys::xui_feature_child(self.handle, index, &mut handle) })?;
        Ok((handle != 0).then(|| Element {
            owner: self.owner.clone(),
            handle,
        }))
    }
    fn choices(&self, choices: &[Choice], selected: Option<u64>) -> Result<()> {
        if choices.len() > 4096 {
            return Err(invalid("Choices exceed 4096 records."));
        }
        let values = choices
            .iter()
            .map(|c| {
                Ok(sys::Choice {
                    size: size_of::<sys::Choice>() as u32,
                    flags: (!c.enabled) as u32,
                    id: c.id,
                    version: c.version,
                    text: text(&c.text)?,
                })
            })
            .collect::<Result<Vec<_>>>()?;
        check(unsafe {
            sys::xui_choices(
                self.handle,
                values.as_ptr(),
                values.len() as u32,
                selected.unwrap_or(0),
                selected.is_some() as u32,
            )
        })
    }
    fn popup(&self, anchor: &Element) -> Result<()> {
        anchor.belongs(&self.owner)?;
        self.owner
            .check(unsafe { sys::xui_popup_show(self.handle, anchor.handle) })
    }
    fn attach_source(&self, source: &ImmutableSource) -> Result<()> {
        source.0.belongs(&self.owner)?;
        self.owner
            .check(unsafe { sys::xui_source_attach(self.handle, source.0.handle) })
    }
    pub fn help(&self, value: &str) -> Result<()> {
        self.feature_set(
            7,
            sys::FeatureValue {
                text: text(value)?,
                ..value_record()
            },
        )
    }
    pub fn tooltip_delay(&self, milliseconds: u32) -> Result<()> {
        self.feature_set(
            8,
            sys::FeatureValue {
                first: milliseconds as u64,
                ..value_record()
            },
        )
    }
    pub fn visible(&self, visible: bool) -> Result<()> {
        self.feature_set(
            37,
            sys::FeatureValue {
                first: visible as u64,
                ..value_record()
            },
        )
    }
}
impl Button {
    pub fn set_icon(&self, icon: ButtonIcon) -> Result<()> {
        self.feature_set(
            45,
            sys::FeatureValue {
                first: icon as u64,
                ..value_record()
            },
        )
    }
    pub fn is_checked(&self) -> Result<bool> {
        Ok(self.feature_get(10)?.first != 0)
    }
    pub fn behavior(&self, behavior: ButtonBehavior) -> Result<()> {
        self.feature_set(
            9,
            sys::FeatureValue {
                first: behavior as u64,
                ..value_record()
            },
        )
    }
    pub fn repeat_timing(&self, delay: u32, interval: u32) -> Result<()> {
        self.feature_set(
            11,
            sys::FeatureValue {
                first: delay as u64,
                second: interval as u64,
                ..value_record()
            },
        )
    }
}
impl RangeInput {
    pub fn on_change(&self, mut callback: impl FnMut(f64) -> Result<()> + 'static) -> Result<()> {
        self.on_event(move |e| {
            if e.kind == 2 {
                callback(f64::from_bits(e.value))?;
            }
            Ok(())
        })
    }
    pub fn change_value(&self, value: f64) -> Result<()> {
        self.feature_action(2, value.to_bits(), 0)
    }
}
impl NumericInput {
    pub fn editor(&self) -> Result<TextInput> {
        self.feature_child(0).map(TextInput)
    }
    pub fn decrease_button(&self) -> Result<Button> {
        self.feature_child(1).map(Button)
    }
    pub fn increase_button(&self) -> Result<Button> {
        self.feature_child(2).map(Button)
    }
    pub fn on_change(&self, mut callback: impl FnMut(f64) -> Result<()> + 'static) -> Result<()> {
        self.on_event(move |e| {
            if e.kind == 2 {
                callback(f64::from_bits(e.value))?;
            }
            Ok(())
        })
    }
    pub fn change_value(&self, value: f64) -> Result<()> {
        self.feature_action(2, value.to_bits(), 0)
    }
    pub fn step(&self, increase: bool) -> Result<()> {
        self.feature_action(3, increase as u64, 0)
    }
}
macro_rules! choices {
    ($($t:ident),*) => {$(
        impl $t {
            pub fn set_items(&self, items: &[Choice], selected: Option<u64>) -> Result<()> { self.0.choices(items,selected) }
            pub fn select(&self, id: u64) -> Result<()> { self.feature_action(1,id,0) }
        }
    )*};
}
choices!(RadioGroup, ComboBox, TabStrip);

impl ComboBox {
    pub fn editor(&self) -> Result<Option<TextInput>> {
        self.optional_feature_child(0).map(|child| child.map(TextInput))
    }
    pub fn popup(&self) -> Result<Popup> {
        self.feature_child(1).map(Popup)
    }
    pub fn choices(&self) -> Result<RadioGroup> {
        self.feature_child(2).map(RadioGroup)
    }
}
impl ColorPicker {
    pub fn channel(&self, index: u32) -> Result<NumericInput> {
        if index >= 4 {
            return Err(invalid("Color channel index must be less than four."));
        }
        self.feature_child(index).map(NumericInput)
    }
    pub fn swatch_button(&self, index: u32) -> Result<Button> {
        let child = index.checked_add(4)
            .ok_or_else(|| invalid("Color swatch index is outside the swatch range."))?;
        self.feature_child(child).map(Button)
    }
}

/// Optional 0xRRGGBB tab colors. None uses the theme; high contrast ignores overrides.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TabColors {
    pub row_background: Option<u32>,
    pub selected_background: Option<u32>,
    pub selected_text: Option<u32>,
    pub inactive_background: Option<u32>,
    pub inactive_text: Option<u32>,
    pub hover_background: Option<u32>,
    pub border: Option<u32>,
}
impl TabStrip {
    pub fn set_duration(&self, milliseconds: u32) -> Result<()> {
        self.owner.check(unsafe { sys::xui_tab_set_duration(self.handle, milliseconds) })
    }
    pub fn duration(&self) -> Result<u32> {
        let mut milliseconds = 0;
        self.owner.check(unsafe { sys::xui_tab_get_duration(self.handle, &mut milliseconds) })?;
        Ok(milliseconds)
    }
    pub fn new_tab_button(&self) -> Result<Button> { self.feature_child(0).map(Button) }
    pub fn set_new_tab_button_visible(&self, visible: bool) -> Result<()> {
        self.owner
            .check(unsafe { sys::xui_tab_set_new_button(self.handle, visible as u32) })
    }
    pub fn new_tab_button_visible(&self) -> Result<bool> {
        let mut visible = 0;
        self.owner
            .check(unsafe { sys::xui_tab_get_new_button(self.handle, &mut visible) })?;
        Ok(visible != 0)
    }
    pub fn set_colors(&self, colors: TabColors) -> Result<()> {
        let values = [
            colors.row_background,
            colors.selected_background,
            colors.selected_text,
            colors.inactive_background,
            colors.inactive_text,
            colors.hover_background,
            colors.border,
        ];
        let mask = values.iter().enumerate().fold(0, |mask, (index, value)| {
            mask | if value.is_some() { 1 << index } else { 0 }
        });
        let value = sys::TabColors {
            size: size_of::<sys::TabColors>() as u32,
            version: 0x10000,
            mask,
            row_background: colors.row_background.unwrap_or(0),
            selected_background: colors.selected_background.unwrap_or(0),
            selected_text: colors.selected_text.unwrap_or(0),
            inactive_background: colors.inactive_background.unwrap_or(0),
            inactive_text: colors.inactive_text.unwrap_or(0),
            hover_background: colors.hover_background.unwrap_or(0),
            border: colors.border.unwrap_or(0),
        };
        self.owner
            .check(unsafe { sys::xui_tab_set_colors(self.handle, &value) })
    }
    pub fn colors(&self) -> Result<TabColors> {
        let mut value = sys::TabColors {
            size: size_of::<sys::TabColors>() as u32,
            version: 0x10000,
            ..Default::default()
        };
        self.owner
            .check(unsafe { sys::xui_tab_get_colors(self.handle, &mut value) })?;
        let read = |bit, color| {
            if value.mask & bit != 0 {
                Some(color)
            } else {
                None
            }
        };
        Ok(TabColors {
            row_background: read(1, value.row_background),
            selected_background: read(2, value.selected_background),
            selected_text: read(4, value.selected_text),
            inactive_background: read(8, value.inactive_background),
            inactive_text: read(16, value.inactive_text),
            hover_background: read(32, value.hover_background),
            border: read(64, value.border),
        })
    }
}
impl Breadcrumb {
    pub fn overflow_button(&self) -> Result<Button> { self.feature_child(0).map(Button) }
    pub fn segment_button(&self, key: ItemKey) -> Result<Button> {
        let mut handle = 0;
        self.owner.check(unsafe { sys::xui_breadcrumb_segment_button(self.handle, key.id, key.version, &mut handle) })?;
        Ok(Button(Element { owner: self.owner.clone(), handle }))
    }
    pub fn set_segments(&self, items: &[Choice]) -> Result<()> {
        self.0.choices(items, None)
    }
}
impl CommandBar {
    pub fn overflow_button(&self) -> Result<Button> { self.feature_child(0).map(Button) }
    pub fn command_button(&self, id: u64) -> Result<Button> {
        let mut handle = 0;
        self.owner.check(unsafe { sys::xui_command_bar_button(self.handle, id, &mut handle) })?;
        Ok(Button(Element { owner: self.owner.clone(), handle }))
    }
}
impl NavigationView {
    pub fn search(&self) -> Result<TextInput> { self.feature_child(0).map(TextInput) }
    pub fn toggle_button(&self) -> Result<Button> { self.feature_child(1).map(Button) }
    pub fn items(&self) -> Result<Element> { self.feature_child(2) }
    pub fn header_items(&self) -> Result<Element> { self.feature_child(3) }
    pub fn footer_items(&self) -> Result<Element> { self.feature_child(4) }
    pub fn title(&self) -> Result<Label> { self.feature_child(5).map(Label) }
    pub fn empty_message(&self) -> Result<Label> { self.feature_child(6).map(Label) }
}
impl SplitButton {
    pub fn primary(&self) -> Result<Button> {
        self.feature_child(0).map(Button)
    }
    pub fn secondary(&self) -> Result<Button> {
        self.feature_child(1).map(Button)
    }
}
macro_rules! popups {
    ($($t:ident),*) => {$(
        impl $t { pub fn show(&self, anchor: &Element) -> Result<()> { self.popup(anchor) } }
    )*};
}
popups!(
    Popup,
    ContentDialog,
    LocationPicker,
    ViewPicker,
    CommandSurface
);
impl ContentDialog {
    pub fn set_validation_message(&self, message: &str) -> Result<()> {
        check(unsafe { sys::xui_dialog_validation(self.handle, text(message)?) })
    }
    pub fn primary(&self) -> Result<Button> {
        self.feature_child(0).map(Button)
    }
    pub fn cancel_button(&self) -> Result<Button> {
        self.feature_child(1).map(Button)
    }
    pub fn title(&self) -> Result<Label> {
        self.feature_child(2).map(Label)
    }
    pub fn validation(&self) -> Result<InlineStatus> {
        self.feature_child(3).map(InlineStatus)
    }
    pub fn body(&self) -> Result<Stack> {
        self.feature_child(4).map(Stack)
    }
    pub fn footer(&self) -> Result<Stack> {
        self.feature_child(5).map(Stack)
    }
}
impl LocationPicker {
    pub fn editor(&self) -> Result<TextInput> {
        self.feature_child(0).map(TextInput)
    }
    pub fn navigation(&self) -> Result<NavigationPane> {
        self.feature_child(1).map(NavigationPane)
    }
    pub fn content(&self) -> Result<Stack> {
        self.feature_child(2).map(Stack)
    }
    pub fn footer(&self) -> Result<Label> {
        self.feature_child(3).map(Label)
    }
    pub fn toolbar(&self) -> Result<CommandBar> {
        self.feature_child(4).map(CommandBar)
    }
}
impl ViewPicker {
    pub fn choices(&self) -> Result<RadioGroup> {
        self.feature_child(0).map(RadioGroup)
    }
    pub fn size(&self) -> Result<RangeInput> {
        self.feature_child(1).map(RangeInput)
    }
    pub fn content(&self) -> Result<Stack> {
        self.feature_child(2).map(Stack)
    }
}
impl CommandSurface {
    pub fn editor(&self) -> Result<TextInput> {
        self.feature_child(0).map(TextInput)
    }
    pub fn title(&self) -> Result<Label> {
        self.feature_child(1).map(Label)
    }
    pub fn status(&self) -> Result<Label> {
        self.feature_child(2).map(Label)
    }
    pub fn close_button(&self) -> Result<Button> {
        self.feature_child(3).map(Button)
    }
    pub fn content(&self) -> Result<Stack> {
        self.feature_child(4).map(Stack)
    }
    pub fn results(&self) -> Result<Stack> {
        self.feature_child(5).map(Stack)
    }
    pub fn menu(&self) -> Result<Element> {
        self.feature_child(6)
    }
}
impl NavigationPane {
    pub fn set_source(&self, source: &ImmutableSource) -> Result<()> {
        self.attach_source(source)
    }
    pub fn items(&self) -> Result<ItemsView> {
        self.feature_child(0).map(ItemsView)
    }
    pub fn status(&self) -> Result<Label> {
        self.feature_child(1).map(Label)
    }
    pub fn content(&self) -> Result<Stack> {
        self.feature_child(2).map(Stack)
    }
    pub fn group(&self) -> Result<Expander> {
        self.feature_child(3).map(Expander)
    }
    pub fn progress(&self) -> Result<Progress> {
        self.feature_child(4).map(Progress)
    }
}
macro_rules! command_controls {
    ($($t:ident),*) => {$(
        impl $t {
            pub fn set_commands(&self, commands: &[Command]) -> Result<()> {
                if commands.len()>4096 { return Err(invalid("Commands exceed 4096 records.")); }
                let records=commands.iter().map(|c|Ok(sys::CommandRecord {size:size_of::<sys::CommandRecord>() as u32,kind:c.kind as u32,
                    id:c.id,parent:c.parent,label:text(&c.label)?,hint:text(&c.shortcut_hint)?,pin_label:text(&c.pin_label)?,
                    flags:(!c.enabled) as u32 | (c.checked==Some(true)) as u32 * 2 | c.checked.is_some() as u32 * 4,icon:0})).collect::<Result<Vec<_>>>()?;
                check(unsafe {sys::xui_commands_set(self.handle,records.as_ptr(),records.len() as u32)})
            }
            pub fn invoke(&self, id: u64, pin: bool) -> Result<()> { self.owner.check(unsafe {sys::xui_command_invoke(self.handle,id,pin as u32)}) }
            pub fn bind(&self, id: u64, key: u32, modifiers: KeyModifiers) -> Result<()> {
                check(unsafe {sys::xui_command_bind(self.handle,id,key,modifiers.control as u32 | (modifiers.shift as u32)<<1 | (modifiers.alt as u32)<<2)})
            }
        }
    )*};
}
command_controls!(CommandBar, CommandSurface);
impl MultilineText {
    pub fn command(&self, command: TextCommand) -> Result<()> {
        self.feature_action(4, command as u64, 0)
    }
}
impl RichText {
    pub fn command(&self, command: TextCommand) -> Result<()> {
        self.feature_action(4, command as u64, 0)
    }
    pub fn set_runs(&self, runs: &[TextRun]) -> Result<()> {
        if runs.len() > 4096 {
            return Err(invalid("Text runs exceed 4096 records."));
        }
        let values = runs
            .iter()
            .map(|r| {
                Ok(sys::TextRun {
                    size: size_of::<sys::TextRun>() as u32,
                    flags: r.bold as u32 | (r.italic as u32) << 1 | (r.underline as u32) << 2,
                    text: text(&r.text)?,
                    link: text(&r.link)?,
                })
            })
            .collect::<Result<Vec<_>>>()?;
        check(unsafe { sys::xui_rich_runs(self.handle, values.as_ptr(), values.len() as u32) })
    }
}
impl PasswordInput {
    pub fn len(&self) -> Result<u64> {
        Ok(self.0.feature_get(16)?.first)
    }
    pub fn is_empty(&self) -> Result<bool> {
        Ok(self.len()? == 0)
    }
    pub fn on_change(&self, mut callback: impl FnMut() -> Result<()> + 'static) -> Result<()> {
        self.0.on_event(move |_| callback())
    }
    pub fn set_password(&self, password: &str) -> Result<()> {
        if password.encode_utf16().count() > 4096 {
            return Err(invalid("Password exceeds 4096 UTF-16 units."));
        }
        self.0.feature_set(
            16,
            sys::FeatureValue {
                text: text(password)?,
                ..value_record()
            },
        )
    }
    /// The borrowed plaintext cannot escape the callback. The binding creates no String copy.
    pub fn with_password(&self, receiver: impl FnOnce(&[u8]) -> Result<()>) -> Result<()> {
        struct Call<F> {
            receiver: Option<F>,
            error: Option<Error>,
        }
        unsafe extern "C" fn invoke<F: FnOnce(&[u8]) -> Result<()>>(
            context: *mut c_void,
            bytes: *const u8,
            length: u32,
        ) -> i32 {
            let call = unsafe { &mut *context.cast::<Call<F>>() };
            let result = catch_unwind(AssertUnwindSafe(|| {
                let slice = if length == 0 {
                    &[][..]
                } else {
                    unsafe { std::slice::from_raw_parts(bytes, length as usize) }
                };
                call.receiver
                    .take()
                    .ok_or_else(|| invalid("Secret callback already consumed."))?(
                    slice
                )
            }));
            match result {
                Ok(Ok(())) => 0,
                Ok(Err(e)) => {
                    call.error = Some(e);
                    8
                }
                Err(payload) => {
                    discard_panic(payload);
                    call.error = Some(invalid("Secret receiver panicked."));
                    8
                }
            }
        }
        fn run<F: FnOnce(&[u8]) -> Result<()>>(e: &Element, receiver: F) -> Result<()> {
            let mut call = Call {
                receiver: Some(receiver),
                error: None,
            };
            let status = unsafe {
                sys::xui_password_read(
                    e.handle,
                    Some(invoke::<F>),
                    (&mut call as *mut Call<F>).cast(),
                )
            };
            if let Some(error) = call.error {
                *e.owner.callback_error.borrow_mut() = Some(error);
            }
            e.owner.check(status)
        }
        run(&self.0, receiver)
    }
}
impl DateTimePicker {
    pub fn set_value(&self, value: LocalDateTime) -> Result<()> {
        if value.year > 9999
            || value.month > 12
            || value.day > 31
            || value.hour > 23
            || value.minute > 59
            || value.second > 59
        {
            return Err(invalid("Invalid local Gregorian fields."));
        }
        self.feature_set(
            18,
            sys::FeatureValue {
                first: (value.year * 10000 + value.month * 100 + value.day) as u64,
                second: (value.hour * 10000 + value.minute * 100 + value.second) as u64,
                ..value_record()
            },
        )
    }
    pub fn value(&self) -> Result<LocalDateTime> {
        let v = self.feature_get(18)?;
        Ok(LocalDateTime {
            year: (v.first / 10000) as u32,
            month: (v.first / 100 % 100) as u32,
            day: (v.first % 100) as u32,
            hour: (v.second / 10000) as u32,
            minute: (v.second / 100 % 100) as u32,
            second: (v.second % 100) as u32,
        })
    }
}
impl InlineStatus {
    pub fn action_button(&self) -> Result<Button> {
        self.feature_child(0).map(Button)
    }
    pub fn dismiss_button(&self) -> Result<Button> {
        self.feature_child(1).map(Button)
    }
    pub fn set_message(&self, message: &str, severity: StatusSeverity) -> Result<()> {
        self.feature_set(
            20,
            sys::FeatureValue {
                text: text(message)?,
                first: severity as u64,
                ..value_record()
            },
        )
    }
}
fn host_state(e: &Element) -> Result<HostState> {
    match e.feature_get(38)?.first {
        0 => Ok(HostState::Idle),
        1 => Ok(HostState::Loading),
        2 => Ok(HostState::Ready),
        3 => Ok(HostState::Playing),
        4 => Ok(HostState::Paused),
        5 => Ok(HostState::Stopped),
        6 => Ok(HostState::Suspended),
        7 => Ok(HostState::Error),
        _ => Err(invalid("Invalid host state.")),
    }
}
impl MediaPlayback {
    pub fn state(&self) -> Result<HostState> {
        host_state(self)
    }
    pub fn load_local(&self, path: &str) -> Result<()> {
        self.feature_set(
            23,
            sys::FeatureValue {
                text: text(path)?,
                ..value_record()
            },
        )
    }
    pub fn seek(&self, seconds: f64) -> Result<()> {
        self.feature_set(
            40,
            sys::FeatureValue {
                a: seconds,
                ..value_record()
            },
        )
    }
}
impl WebContent {
    pub fn state(&self) -> Result<HostState> {
        host_state(self)
    }
    pub fn set_html(&self, html: &str) -> Result<()> {
        self.feature_set(
            25,
            sys::FeatureValue {
                text: text(html)?,
                ..value_record()
            },
        )
    }
    pub fn set_profile_root(&self, path: &str) -> Result<()> {
        self.feature_set(
            26,
            sys::FeatureValue {
                text: text(path)?,
                ..value_record()
            },
        )
    }
    pub fn set_allowed_origins(&self, origins: &[&str]) -> Result<()> {
        if origins.len() > 16 {
            return Err(invalid("At most 16 origins are supported."));
        }
        let values = origins
            .iter()
            .map(|s| text(s))
            .collect::<Result<Vec<_>>>()?;
        check(unsafe { sys::xui_web_origins(self.handle, values.as_ptr(), values.len() as u32) })
    }
    pub fn navigate(&self, uri: &str) -> Result<()> {
        check(unsafe { sys::xui_web_navigate(self.handle, text(uri)?) })
    }
    pub fn evaluate(&self, script: &str) -> Result<WebEvaluation> {
        let mut handle = 0;
        check(unsafe { sys::xui_web_evaluate(self.handle, text(script)?, &mut handle) })?;
        Ok(WebEvaluation {
            owner: self.owner.clone(),
            handle,
        })
    }
}
#[derive(Debug)]
pub struct WebResult {
    pub text: String,
    pub is_error: bool,
}
pub struct WebEvaluation {
    owner: Rc<Inner>,
    handle: u64,
}
impl WebEvaluation {
    pub fn try_result(&self) -> Result<Option<WebResult>> {
        let mut count = 0;
        let mut failed = 0;
        let status = unsafe {
            sys::xui_web_result(
                self.handle,
                std::ptr::null_mut(),
                0,
                &mut count,
                &mut failed,
            )
        };
        if status == 7 {
            return Ok(None);
        }
        if status != 6 {
            self.owner.check(status)?;
        }
        let mut bytes = vec![0u8; count as usize];
        self.owner.check(unsafe {
            sys::xui_web_result(
                self.handle,
                bytes.as_mut_ptr(),
                count,
                &mut count,
                &mut failed,
            )
        })?;
        Ok(Some(WebResult {
            text: String::from_utf8(bytes).map_err(|_| invalid("Invalid UTF-8 result."))?,
            is_error: failed != 0,
        }))
    }
}
impl Drop for WebEvaluation {
    fn drop(&mut self) {
        unsafe {
            sys::xui_request_cancel(self.handle);
        }
    }
}
impl HistoryChart {
    pub fn append(&self, value: f64) -> Result<()> {
        self.feature_set(
            2,
            sys::FeatureValue {
                a: value,
                ..value_record()
            },
        )
    }
}
fn panel_add(
    e: &Element,
    child: &Element,
    row: u32,
    column: u32,
    rows: u32,
    columns: u32,
) -> Result<()> {
    child.belongs(&e.owner)?;
    check(unsafe { sys::xui_panel_add(e.handle, child.handle, row, column, rows, columns) })
}
impl Wrap {
    pub fn add(&self, child: &Element) -> Result<()> {
        panel_add(self, child, 0, 0, 1, 1)
    }
}
impl PageView {
    pub fn add(&self, child: &Element) -> Result<()> {
        panel_add(self, child, 0, 0, 1, 1)
    }
}
impl Grid {
    pub fn add(
        &self,
        child: &Element,
        row: u32,
        column: u32,
        rows: u32,
        columns: u32,
    ) -> Result<()> {
        panel_add(self, child, row, column, rows, columns)
    }
    pub fn set_tracks(&self, rows: &[GridTrack], columns: &[GridTrack]) -> Result<()> {
        fn convert(v: &[GridTrack]) -> Result<Vec<sys::GridTrack>> {
            if v.len() > 256 {
                return Err(invalid("A grid supports at most 256 tracks."));
            }
            Ok(v.iter()
                .map(|t| sys::GridTrack {
                    size: size_of::<sys::GridTrack>() as u32,
                    sizing: t.sizing as u32,
                    value: t.value,
                    minimum: t.minimum,
                    maximum: t.maximum,
                    reserved: 0,
                })
                .collect())
        }
        let r = convert(rows)?;
        let c = convert(columns)?;
        check(unsafe {
            sys::xui_grid_tracks(
                self.handle,
                r.as_ptr(),
                r.len() as u32,
                c.as_ptr(),
                c.len() as u32,
            )
        })
    }
}
impl DataGrid {
    pub fn selection(&self) -> Result<SelectionInfo> {
        self.collection_selection()
    }
    pub fn contains(&self, key: ItemKey) -> Result<bool> {
        self.collection_contains(key)
    }
    pub fn set_filter(&self, source_column: u32, query: &str) -> Result<()> {
        check(unsafe { sys::xui_grid_filter(self.handle, source_column, text(query)?) })
    }
    pub fn set_sort(&self, source_column: u32, descending: bool) -> Result<()> {
        check(unsafe { sys::xui_grid_sort(self.handle, source_column, descending as u32) })
    }
    pub fn set_checked(&self, key: ItemKey, checked: bool) -> Result<()> {
        self.owner
            .check(unsafe { sys::xui_grid_check(self.handle, key.id, key.version, checked as u32) })
    }
    pub fn set_source(&self, source: &ImmutableSource) -> Result<()> {
        self.attach_source(source)
    }
    pub fn select(&self, key: ItemKey) -> Result<()> {
        self.feature_action(1, key.id, key.version)
    }
    pub fn set_columns(&self, columns: &[GridColumn]) -> Result<()> {
        if columns.len() > 256 {
            return Err(invalid("A grid supports at most 256 columns."));
        }
        let values = columns
            .iter()
            .map(|c| {
                Ok(sys::Column {
                    size: size_of::<sys::Column>() as u32,
                    flags: c.numeric as u32
                        | (c.filterable as u32) << 1
                        | (c.checkable as u32) << 2,
                    name: text(&c.name)?,
                    width: c.width,
                    reserved: 0,
                })
            })
            .collect::<Result<Vec<_>>>()?;
        check(unsafe { sys::xui_grid_columns(self.handle, values.as_ptr(), values.len() as u32) })
    }
    pub fn set_column_width(&self, column: u32, width: f32) -> Result<()> {
        check(unsafe { sys::xui_grid_column_width(self.handle, column, width) })
    }
    pub fn set_column_order(&self, order: &[u32]) -> Result<()> {
        if order.len() > 256 {
            return Err(invalid("Column order exceeds 256 entries."));
        }
        check(unsafe {
            sys::xui_grid_column_order(self.handle, order.as_ptr(), order.len() as u32)
        })
    }
}
impl ItemsView {
    pub fn selection(&self) -> Result<SelectionInfo> {
        self.collection_selection()
    }
    pub fn contains(&self, key: ItemKey) -> Result<bool> {
        self.collection_contains(key)
    }
    pub fn set_source(&self, source: &ImmutableSource) -> Result<()> {
        self.attach_source(source)
    }
    pub fn select(&self, key: ItemKey) -> Result<()> {
        self.feature_action(1, key.id, key.version)
    }
    pub fn item_size(&self, width: f64, height: f64) -> Result<()> {
        self.feature_set(
            27,
            sys::FeatureValue {
                a: width,
                b: height,
                ..value_record()
            },
        )
    }
}
impl TreeView {
    pub fn selection(&self) -> Result<SelectionInfo> {
        self.collection_selection()
    }
    pub fn contains(&self, key: ItemKey) -> Result<bool> {
        self.collection_contains(key)
    }
    pub fn set_source(&self, source: &ImmutableSource) -> Result<()> {
        self.attach_source(source)
    }
    pub fn expand(&self, key: ItemKey, expanded: bool) -> Result<()> {
        self.owner.check(unsafe {
            sys::xui_tree_expand(self.handle, key.id, key.version, expanded as u32)
        })
    }
    pub fn on_request(
        &self,
        mut callback: impl FnMut(TreeRequest) -> Result<()> + 'static,
    ) -> Result<()> {
        let weak = self.downgrade();
        self.on_event(move |e| {
            if e.kind == 11
                && let Some(owner) = weak.upgrade()
            {
                callback(TreeRequest {
                    owner,
                    handle: e.value,
                })?;
            }
            Ok(())
        })
    }
}
pub struct TreeRequest {
    owner: Element,
    handle: u64,
}
impl TreeRequest {
    pub fn node(&self) -> Result<ItemKey> {
        let mut value = value_record();
        check(unsafe { sys::xui_request_info(self.handle, &mut value) })?;
        Ok(ItemKey {
            id: value.first,
            version: value.second,
        })
    }
    pub fn complete(mut self, source: &ImmutableSource, message: &str) -> Result<()> {
        source.0.belongs(&self.owner.owner)?;
        let status = unsafe {
            sys::xui_tree_complete(
                self.owner.handle,
                self.handle,
                source.0.handle,
                text(message)?,
            )
        };
        if status == 0 || status == 11 {
            self.handle = 0;
        }
        self.owner.owner.check(status)
    }
}
impl Drop for TreeRequest {
    fn drop(&mut self) {
        if self.handle != 0 {
            unsafe {
                sys::xui_request_cancel(self.handle);
            }
        }
    }
}

#[derive(Clone, Debug)]
pub struct ItemContent {
    pub primary: String,
    pub secondary: String,
    pub enabled: bool,
    pub progress: Option<f64>,
    pub checked: Option<bool>,
}
/// Immutable, UI-thread-only, constant-cost identity lookups. No source enumeration occurs.
pub trait ReadOnlyImmutableSource {
    fn count(&self) -> u64;
    fn key(&self, index: u64) -> Result<ItemKey>;
    fn find(&self, key: ItemKey) -> Result<Option<u64>>;
    fn item(&self, index: u64, column: u64) -> Result<ItemContent>;
    fn has_children(&self, _key: ItemKey) -> Result<bool> {
        Ok(false)
    }
}
#[derive(Clone)]
pub struct ImmutableSource(Rc<SourceLease>);
struct SourceLease(Element);
impl std::ops::Deref for SourceLease {
    type Target = Element;
    fn deref(&self) -> &Element {
        &self.0
    }
}
impl Drop for SourceLease {
    fn drop(&mut self) {
        unsafe {
            sys::xui_source_release(self.handle);
        }
    }
}
struct SourcePin {
    source: Box<dyn ReadOnlyImmutableSource>,
    owner: Weak<Inner>,
}
unsafe extern "C" fn retain_source(context: *mut c_void) {
    unsafe {
        Rc::increment_strong_count(context.cast::<SourcePin>());
    }
}
unsafe extern "C" fn release_source(context: *mut c_void) {
    if let Err(payload) = catch_unwind(AssertUnwindSafe(|| unsafe {
        drop(Rc::from_raw(context.cast::<SourcePin>()));
    })) {
        discard_panic(payload);
    }
}
unsafe extern "C" fn query_source(
    context: *mut c_void,
    operation: u32,
    first: u64,
    second: u64,
    row: *mut sys::SourceRow,
) -> i32 {
    let pin = unsafe { &*context.cast::<SourcePin>() };
    let Some(owner) = pin.owner.upgrade() else {
        return 11;
    };
    let result = catch_unwind(AssertUnwindSafe(|| -> Result<()> {
        let r = unsafe { &mut *row };
        match operation {
            0 => {
                let key = pin.source.key(first)?;
                r.id = key.id;
                r.version = key.version;
            }
            1 => {
                let item = pin.source.item(first, second)?;
                fn copy(s: &str, output: &mut [u8; 1024]) -> Result<u32> {
                    text(s)?;
                    if s.len() > 1024 {
                        return Err(invalid("A source field exceeds 1024 UTF-8 bytes."));
                    }
                    output[..s.len()].copy_from_slice(s.as_bytes());
                    Ok(s.len() as u32)
                }
                r.primary_length = copy(&item.primary, &mut r.primary)?;
                r.secondary_length = copy(&item.secondary, &mut r.secondary)?;
                r.flags = (!item.enabled) as u32
                    | (item.progress.is_some() as u32) << 1
                    | ((item.checked == Some(true)) as u32) << 2
                    | (item.checked.is_some() as u32) << 3;
                r.progress = item.progress.unwrap_or(0.);
            }
            2 => {
                r.index = pin
                    .source
                    .find(ItemKey {
                        id: first,
                        version: second,
                    })?
                    .unwrap_or(u64::MAX)
            }
            3 => {
                r.index = pin.source.has_children(ItemKey {
                    id: first,
                    version: second,
                })? as u64
            }
            _ => return Err(invalid("Unknown source query.")),
        }
        Ok(())
    }));
    let error = match result {
        Ok(Ok(())) => return 0,
        Ok(Err(e)) => e,
        Err(payload) => {
            discard_panic(payload);
            invalid("Immutable source callback panicked.")
        }
    };
    *owner.callback_error.borrow_mut() = Some(error);
    8
}
impl Window {
    pub fn immutable_source(
        &self,
        source: impl ReadOnlyImmutableSource + 'static,
    ) -> Result<ImmutableSource> {
        let count = source.count();
        let pin = Rc::new(SourcePin {
            source: Box::new(source),
            owner: Rc::downgrade(&self.0),
        });
        let context = Rc::into_raw(pin).cast_mut().cast::<c_void>();
        let options = sys::SourceOptions {
            size: size_of::<sys::SourceOptions>() as u32,
            version: 0x10001,
            count,
            context,
            query: Some(query_source),
            retain: Some(retain_source),
            release: Some(release_source),
        };
        let mut handle = 0;
        let status = unsafe { sys::xui_source_create(self.0.handle, &options, &mut handle) };
        unsafe {
            release_source(context);
        }
        check(status)?;
        Ok(ImmutableSource(Rc::new(SourceLease(Element {
            owner: self.0.clone(),
            handle,
        }))))
    }
}
fn markers(values: &[MapMarker]) -> Result<Vec<sys::MapMarker>> {
    if values.len() > 256 {
        return Err(invalid("A map supports at most 256 markers."));
    }
    values
        .iter()
        .map(|v| {
            Ok(sys::MapMarker {
                size: size_of::<sys::MapMarker>() as u32,
                reserved: 0,
                id: v.id,
                latitude: v.location.latitude,
                longitude: v.location.longitude,
                name: text(&v.name)?,
            })
        })
        .collect()
}
impl MapView {
    pub fn set_view(&self, center: GeoPoint, zoom: f64) -> Result<()> {
        self.feature_set(
            22,
            sys::FeatureValue {
                a: center.latitude,
                b: center.longitude,
                c: zoom,
                ..value_record()
            },
        )
    }
    pub fn view(&self) -> Result<(GeoPoint, f64)> {
        let v = self.feature_get(22)?;
        Ok((
            GeoPoint {
                latitude: v.a,
                longitude: v.b,
            },
            v.c,
        ))
    }
    pub fn pan(&self, x: f64, y: f64) -> Result<()> {
        self.feature_set(
            39,
            sys::FeatureValue {
                a: x,
                b: y,
                ..value_record()
            },
        )
    }
    pub fn set_markers(&self, values: &[MapMarker]) -> Result<()> {
        let values = markers(values)?;
        check(unsafe { sys::xui_map_markers(self.handle, values.as_ptr(), values.len() as u32) })
    }
    pub fn request_overlay(&self) -> Result<MapRequest> {
        let mut handle = 0;
        check(unsafe { sys::xui_map_request(self.handle, &mut handle) })?;
        Ok(MapRequest {
            owner: self.0.clone(),
            handle,
        })
    }
}
pub struct MapRequest {
    owner: Element,
    handle: u64,
}
impl MapRequest {
    pub fn complete(mut self, values: &[MapMarker]) -> Result<()> {
        let values = markers(values)?;
        let status = unsafe {
            sys::xui_map_complete(
                self.owner.handle,
                self.handle,
                values.as_ptr(),
                values.len() as u32,
            )
        };
        if status == 0 || status == 11 {
            self.handle = 0;
        }
        check(status)
    }
}
impl Drop for MapRequest {
    fn drop(&mut self) {
        if self.handle != 0 {
            unsafe {
                sys::xui_request_cancel(self.handle);
            }
        }
    }
}
impl VectorCanvas {
    pub fn set_scene(&self, shapes: &[VectorShape]) -> Result<()> {
        if shapes.len() > 4096
            || shapes
                .iter()
                .try_fold(0usize, |n, s| n.checked_add(s.points.len()))
                .is_none_or(|n| n > 65536)
        {
            return Err(invalid("Vector scene exceeds its shape or point limit."));
        }
        let points: Vec<Vec<sys::ScenePoint>> = shapes
            .iter()
            .map(|s| {
                s.points
                    .iter()
                    .map(|p| sys::ScenePoint { x: p.x, y: p.y })
                    .collect()
            })
            .collect();
        let values = shapes
            .iter()
            .zip(&points)
            .map(|(s, p)| {
                let c = s.clip.unwrap_or_default();
                let t = s.transform;
                Ok(sys::Shape {
                    size: size_of::<sys::Shape>() as u32,
                    flags: s.closed as u32
                        | (s.interactive as u32) << 1
                        | (s.clip.is_some() as u32) << 2,
                    id: s.id,
                    points: p.as_ptr(),
                    count: p.len() as u32,
                    reserved: 0,
                    reserved2: 0,
                    name: text(&s.name)?,
                    fill_red: s.fill.red,
                    fill_green: s.fill.green,
                    fill_blue: s.fill.blue,
                    fill_alpha: s.fill.alpha,
                    stroke_red: s.stroke.red,
                    stroke_green: s.stroke.green,
                    stroke_blue: s.stroke.blue,
                    stroke_alpha: s.stroke.alpha,
                    stroke_width: s.stroke_width,
                    clip_x: c.x,
                    clip_y: c.y,
                    clip_width: c.width,
                    clip_height: c.height,
                    m11: t.m11,
                    m12: t.m12,
                    m21: t.m21,
                    m22: t.m22,
                    dx: t.dx,
                    dy: t.dy,
                })
            })
            .collect::<Result<Vec<_>>>()?;
        check(unsafe { sys::xui_canvas_scene(self.handle, values.as_ptr(), values.len() as u32) })
    }
}
