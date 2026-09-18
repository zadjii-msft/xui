(() => {
  "use strict";

  const $ = id => document.getElementById(id);
  const data = window.solarExplorations;
  const slug = value => typeof value === "string" && /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(value);
  const text = value => typeof value === "string" && value.length > 0;
  const unique = items => new Set(items.map(item => item.id)).size === items.length;

  function validMetadata() {
    if (!data || !Array.isArray(data.manes) || !Array.isArray(data.faces) ||
        !Array.isArray(data.palettes) || !Array.isArray(data.variants)) return false;
    if (data.manes.length !== 12 || data.faces.length !== 4 ||
        data.palettes.length !== 7 || data.variants.length !== 48) return false;
    if (![data.manes, data.faces, data.palettes, data.variants].every(items =>
      items.every(item => item && slug(item.id) && text(item.name)) && unique(items))) return false;
    if (!data.manes.every(item => text(item.letter) && text(item.note)) ||
        !data.faces.every(item => text(item.note) && Number.isFinite(item.scaleX) && Number.isFinite(item.scaleY)) ||
        !data.palettes.every(item => text(item.label)) ||
        !data.palettes.some(item => item.id === "rainbow")) return false;
    return data.variants.every(item => slug(item.stem) && text(item.label)) &&
      data.manes.every(mane => data.faces.every(face =>
        data.variants.filter(item => item.mane === mane.id && item.face === face.id).length === 1));
  }

  if (!validMetadata()) {
    $("load-error").hidden = false;
    $("load-error").textContent = "Cannot load the 48 Solar studies. Metadata is missing or invalid. Keep studies.js beside this page, then reload.";
    return;
  }

  const shortlist = new Set();
  const failedFiles = new Set();
  const cards = new Map();
  const rows = new Map();
  let palette = data.palettes.find(item => item.id === "rainbow");
  let selected = null;
  let shortlistOnly = false;
  let opener = null;

  function element(tag, className, content) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (content !== undefined) node.textContent = content;
    return node;
  }

  const fileFor = variant => `${variant.stem}-${palette.id}.svg`;

  function image(variant, size) {
    const wrapper = element("div", "image-wrapper");
    const img = element("img", "preview-image");
    img.width = size;
    img.height = size;
    img.style.width = `${size}px`;
    img.style.height = `${size}px`;
    img.alt = `${variant.label}: ${variant.name}, ${palette.name} / ${palette.label}`;
    const filename = fileFor(variant);
    img.addEventListener("error", () => {
      img.hidden = true;
      wrapper.append(element("span", "image-failure", `SVG unavailable: ${filename}`));
      failedFiles.add(filename);
      $("image-error").hidden = false;
      $("image-error").textContent = `Cannot load ${failedFiles.size} SVG file${failedFiles.size === 1 ? "" : "s"}. First missing file: ${failedFiles.values().next().value}. Keep all SVG files beside this page, then reload.`;
    }, { once: true });
    img.src = filename;
    wrapper.append(img);
    return wrapper;
  }

  function option(select, value, label) {
    const node = element("option", "", label);
    node.value = value;
    select.append(node);
  }

  for (const item of data.palettes) option($("palette"), item.id, `${item.name} / ${item.label}`);
  for (const item of data.manes) option($("mane-filter"), item.id, `${item.letter} / ${item.name}`);
  for (const [index, item] of data.faces.entries()) option($("face-filter"), item.id, `${index + 1} / ${item.name}`);
  $("palette").value = palette.id;

  for (const mane of data.manes) {
    const row = element("section", "mane-row matrix-grid");
    row.id = `mane-${mane.id}`;
    const heading = element("div", "row-heading");
    heading.append(element("span", "row-letter", `MANE ${mane.letter}`));
    const title = element("h3", "", mane.name);
    title.id = `heading-${mane.id}`;
    row.setAttribute("aria-labelledby", title.id);
    heading.append(title, element("p", "", mane.note));
    row.append(heading);
    rows.set(mane.id, row);
    for (const face of data.faces) {
      const variant = data.variants.find(item => item.mane === mane.id && item.face === face.id);
      const card = element("article", "variant");
      card.dataset.variant = variant.id;
      const inspect = element("button", "inspect-choice");
      inspect.type = "button";
      inspect.setAttribute("aria-label", `Inspect ${variant.label}: ${variant.name}`);
      inspect.setAttribute("aria-haspopup", "dialog");
      inspect.setAttribute("aria-controls", "inspector");
      inspect.setAttribute("aria-pressed", "false");
      const board = element("div", "artboard thumbnail");
      board.append(image(variant, 180));
      const caption = element("span", "caption");
      caption.append(element("strong", "", variant.label), element("span", "", face.name));
      inspect.append(board, caption, element("span", "inspect-hint", "Inspect shape ↗"));
      inspect.addEventListener("click", () => openInspector(variant, inspect));
      const save = element("button", "shortlist-choice", "Shortlist");
      save.type = "button";
      save.setAttribute("aria-label", `Shortlist ${variant.label}`);
      save.setAttribute("aria-pressed", "false");
      save.addEventListener("click", () => toggleShortlist(variant));
      card.append(inspect, save);
      row.append(card);
      cards.set(variant.id, { card, inspect, board, save, variant });
    }
    $("matrix").append(row);
  }

  function updateFilters() {
    const maneId = $("mane-filter").value;
    const faceId = $("face-filter").value;
    const faces = data.faces.filter(face => !faceId || face.id === faceId);
    document.documentElement.style.setProperty("--face-count", faces.length);
    $("column-headings").replaceChildren(element("div", "", "Mane / Face →"));
    for (const face of faces) {
      const heading = element("div", "column-heading", `${data.faces.indexOf(face) + 1} / ${face.name}`);
      heading.append(element("span", "", face.note));
      $("column-headings").append(heading);
    }
    let count = 0;
    for (const mane of data.manes) {
      const row = rows.get(mane.id);
      let rowCount = 0;
      for (const entry of cards.values()) {
        if (entry.variant.mane !== mane.id) continue;
        const matches = (!maneId || maneId === mane.id) && (!faceId || faceId === entry.variant.face);
        const visible = matches && (!shortlistOnly || shortlist.has(entry.variant.id));
        entry.card.hidden = !matches;
        entry.card.classList.toggle("empty-slot", matches && !visible);
        entry.inspect.hidden = !visible;
        entry.save.hidden = !visible;
        if (visible) rowCount++;
      }
      row.hidden = rowCount === 0;
      count += rowCount;
    }
    $("results").textContent = `${count} of 48 shapes · ${palette.name} / ${palette.label}`;
    $("empty").hidden = count !== 0;
    $("column-headings").hidden = count === 0;
  }

  function updateShortlist() {
    const saved = data.variants.filter(item => shortlist.has(item.id));
    $("shortlist-summary").textContent = saved.length
      ? `${saved.length} selected: ${saved.map(item => item.label).join(", ")}. Selections last until this page reloads.`
      : "No selections. Use “Shortlist” below a drawing. Selections last until this page reloads.";
    $("clear-shortlist").disabled = saved.length === 0;
    for (const [id, entry] of cards) {
      const active = shortlist.has(id);
      entry.card.classList.toggle("shortlisted", active);
      entry.save.setAttribute("aria-pressed", String(active));
      entry.save.textContent = active ? "✓ Shortlisted" : "Shortlist";
    }
    if (selected) {
      const active = shortlist.has(selected.id);
      $("inspect-shortlist").setAttribute("aria-pressed", String(active));
      $("inspect-shortlist").textContent = active ? "✓ Shortlisted" : "Shortlist";
      $("inspect-shortlist").setAttribute("aria-label", `Shortlist ${selected.label}`);
    }
    const focused = document.activeElement;
    updateFilters();
    if (focused && focused.closest(".variant") && !focused.checkVisibility()) $("shortlist-only").focus();
  }

  function toggleShortlist(variant) {
    if (shortlist.has(variant.id)) shortlist.delete(variant.id);
    else shortlist.add(variant.id);
    updateShortlist();
  }

  function setPalette(id) {
    palette = data.palettes.find(item => item.id === id);
    $("palette").value = palette.id;
    for (const entry of cards.values()) entry.board.replaceChildren(image(entry.variant, 180));
    if (selected) updateInspectionImages();
    updateFilters();
  }

  function updateInspectionImages() {
    $("large-preview").replaceChildren(image(selected, 340));
    const large = $("large-preview").querySelector("img");
    large.style.width = "100%";
    large.style.height = "auto";
    $("sample-24").replaceChildren(image(selected, 24));
    $("sample-48").replaceChildren(image(selected, 48));
    $("download").href = fileFor(selected);
    $("download").download = fileFor(selected);
    $("download").textContent = `Download ${selected.label} · ${palette.name} SVG`;
    for (const button of $("inspect-palettes").children) {
      button.setAttribute("aria-pressed", String(button.dataset.palette === palette.id));
    }
  }

  function openInspector(variant, trigger) {
    selected = variant;
    opener = trigger;
    const mane = data.manes.find(item => item.id === variant.mane);
    const face = data.faces.find(item => item.id === variant.face);
    $("inspect-title").textContent = `${variant.label} / ${variant.name}`;
    $("inspect-description").textContent = `${mane.note} ${face.note}`;
    $("inspect-facts").replaceChildren(
      element("dt", "", `Mane ${mane.letter} / ${mane.name}`),
      element("dd", "", "Six separate segments. Open below the chin."),
      element("dt", "", `Face ${data.faces.indexOf(face) + 1} / ${face.name}`),
      element("dd", "", `Horizontal scale ${face.scaleX}× · Vertical scale ${face.scaleY}×`));
    for (const entry of cards.values()) entry.inspect.setAttribute("aria-pressed", String(entry.variant.id === variant.id));
    updateInspectionImages();
    updateShortlist();
    $("inspector").showModal();
  }

  for (const item of data.palettes) {
    const button = element("button", "", item.name);
    button.type = "button";
    button.dataset.palette = item.id;
    button.setAttribute("aria-label", `${item.name} / ${item.label}`);
    button.setAttribute("aria-pressed", String(item.id === palette.id));
    button.append(element("small", "", item.label));
    button.addEventListener("click", () => setPalette(item.id));
    $("inspect-palettes").append(button);
  }
  $("palette").addEventListener("change", event => setPalette(event.target.value));
  $("mane-filter").addEventListener("change", updateFilters);
  $("face-filter").addEventListener("change", updateFilters);
  $("background").addEventListener("click", event => {
    const active = document.body.classList.toggle("dark");
    event.currentTarget.setAttribute("aria-pressed", String(active));
  });
  $("shortlist-only").addEventListener("click", event => {
    shortlistOnly = !shortlistOnly;
    event.currentTarget.setAttribute("aria-pressed", String(shortlistOnly));
    updateFilters();
  });
  $("clear-shortlist").addEventListener("click", () => {
    shortlist.clear();
    updateShortlist();
    $("shortlist-only").focus();
  });
  $("reset-filters").addEventListener("click", () => {
    $("mane-filter").value = "";
    $("face-filter").value = "";
    shortlistOnly = false;
    $("shortlist-only").setAttribute("aria-pressed", "false");
    updateFilters();
  });
  $("inspect-shortlist").addEventListener("click", () => toggleShortlist(selected));
  $("close-inspector").addEventListener("click", () => $("inspector").close());
  $("inspector").addEventListener("close", () => {
    if (opener && opener.checkVisibility()) opener.focus();
    else $("shortlist-only").focus();
  });
  $("controls").disabled = false;
  updateFilters();
})();
