(() => {
  "use strict";
  const $ = id => document.getElementById(id);
  const families = ["Graphic & minimal", "Material & craft", "Expressive & playful"];
  const raw = window.solarD2Styles;
  const text = value => typeof value === "string" && value.trim().length > 0;
  const valid = Array.isArray(raw) && raw.length === 24 &&
    raw.every(item => item && /^s\d{2}$/.test(item.id) && item.label === item.id.toUpperCase() &&
      /^\d{2}-[a-z0-9-]+\.svg$/.test(item.file) && text(item.name) && text(item.note) && families.includes(item.family)) &&
    new Set(raw.map(item => item.id)).size === 24 &&
    new Set(raw.map(item => item.file)).size === 24 &&
    new Set(raw.map(item => item.name)).size === 24 &&
    families.every(family => raw.filter(item => item.family === family).length === 8);
  const styles = valid ? [...raw].sort((a, b) => a.id.localeCompare(b.id)) : [];
  if (!valid || styles.some((style, index) => style.id !== `s${String(index + 1).padStart(2, "0")}`)) {
    $("load-error").hidden = false;
    $("load-error").textContent = "Cannot load the 24 D2 styles. Keep all three styles-*.js files beside this page, then reload.";
    $("results").textContent = "Style collection unavailable.";
    return;
  }

  const shortlist = new Set();
  const cards = new Map();
  let selected = null;
  let opener = null;
  let shortlistOnly = false;

  function element(tag, className, content) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (content !== undefined) node.textContent = content;
    return node;
  }

  function reportImageError(image) {
    const file = image.getAttribute("src");
    $("image-error").hidden = false;
    $("image-error").textContent = `Cannot load ${file}. Keep the SVG files beside this page, then reload.`;
  }

  function image(style, size) {
    const img = element("img");
    img.width = size;
    img.height = size;
    img.alt = `${style.label}: ${style.name}, front-facing D2 lion interpretation`;
    img.addEventListener("error", () => {
      reportImageError(img);
      img.hidden = true;
      img.parentElement.append(element("span", "image-failure", `SVG unavailable: ${style.file}`));
    }, { once: true });
    img.src = style.file;
    return img;
  }

  function updateFilters() {
    const query = $("search").value.trim().toLowerCase();
    const number = query.match(/^s?0*(\d+)$/);
    let count = 0;
    for (const { card, style } of cards.values()) {
      const matchesSearch = number
        ? Number(style.id.slice(1)) === Number(number[1])
        : query.split(/\s+/).every(term => `${style.name} ${style.note} ${style.family}`.toLowerCase().includes(term));
      card.hidden = !matchesSearch || ($("family").value && $("family").value !== style.family) ||
        (shortlistOnly && !shortlist.has(style.id));
      if (!card.hidden) count++;
    }
    $("results").textContent = `${count} of 24 styles`;
    $("empty").hidden = count > 0;
  }

  function updateShortlist() {
    for (const { card, save, style } of cards.values()) {
      const active = shortlist.has(style.id);
      card.classList.toggle("shortlisted", active);
      save.setAttribute("aria-pressed", String(active));
      save.textContent = active ? "Shortlisted" : "Shortlist";
    }
    const saved = styles.filter(style => shortlist.has(style.id));
    $("shortlist-summary").textContent = saved.length
      ? `${saved.map(style => `${style.label} / ${style.name}`).join("; ")}. Selections last until reload.`
      : "No selections. Shortlists last until reload.";
    $("clear-shortlist").disabled = saved.length === 0;
    if (selected) {
      const active = shortlist.has(selected.id);
      $("inspect-shortlist").setAttribute("aria-pressed", String(active));
      $("inspect-shortlist").textContent = active ? "Shortlisted" : "Shortlist";
      $("inspect-shortlist").setAttribute("aria-label", `Shortlist ${selected.label}`);
    }
    const focused = document.activeElement;
    updateFilters();
    if (focused?.closest(".card") && !focused.checkVisibility()) $("shortlist-only").focus();
  }

  function toggleShortlist(style) {
    if (shortlist.has(style.id)) shortlist.delete(style.id);
    else shortlist.add(style.id);
    updateShortlist();
  }

  function inspect(style, trigger) {
    selected = style;
    opener = trigger;
    $("inspect-title").textContent = `${style.label} / ${style.name}`;
    $("inspect-caption").textContent = `${style.family} / New interpretation`;
    $("inspect-note").textContent = style.note;
    $("large-preview").replaceChildren(image(style, 280));
    for (const size of [24, 48]) {
      const board = element("div", "artboard");
      board.append(image(style, size));
      $(`sample-${size}`).replaceChildren(board);
    }
    $("download").href = style.file;
    $("download").download = style.file;
    $("download").textContent = `Download ${style.label} SVG`;
    updateShortlist();
    $("inspector").showModal();
  }

  for (const style of styles) {
    const card = element("article", "card");
    card.dataset.style = style.id;
    const inspectButton = element("button", "inspect");
    inspectButton.type = "button";
    inspectButton.setAttribute("aria-label", `Inspect ${style.label}: ${style.name}`);
    inspectButton.setAttribute("aria-haspopup", "dialog");
    inspectButton.setAttribute("aria-controls", "inspector");
    const board = element("span", "artboard");
    board.append(element("span", "style-id", style.label), image(style, 210));
    inspectButton.append(board, element("span", "inspect-hint", "Compare with D2"));
    inspectButton.addEventListener("click", () => inspect(style, inspectButton));
    const body = element("div", "card-body");
    const title = element("h3", "", style.name);
    title.id = `title-${style.id}`;
    card.setAttribute("aria-labelledby", title.id);
    body.append(element("span", "family", style.family), title, element("p", "", style.note));
    const actions = element("div", "card-actions");
    const save = element("button", "", "Shortlist");
    save.type = "button";
    save.setAttribute("aria-label", `Shortlist ${style.label}`);
    save.setAttribute("aria-pressed", "false");
    save.addEventListener("click", () => toggleShortlist(style));
    const download = element("a", "", "Save SVG");
    download.href = style.file;
    download.download = style.file;
    actions.append(save, download);
    body.append(actions);
    card.append(inspectButton, body);
    $("gallery").append(card);
    cards.set(style.id, { card, save, style });
  }

  $("search").addEventListener("input", updateFilters);
  $("family").addEventListener("change", updateFilters);
  $("background").addEventListener("click", event => {
    event.currentTarget.setAttribute("aria-pressed", String(document.body.classList.toggle("dark")));
  });
  $("shortlist-only").addEventListener("click", event => {
    shortlistOnly = !shortlistOnly;
    event.currentTarget.setAttribute("aria-pressed", String(shortlistOnly));
    updateFilters();
  });
  $("reset").addEventListener("click", () => {
    $("search").value = "";
    $("family").value = "";
    shortlistOnly = false;
    $("shortlist-only").setAttribute("aria-pressed", "false");
    updateFilters();
  });
  $("clear-shortlist").addEventListener("click", () => {
    shortlist.clear();
    updateShortlist();
    $("shortlist-only").focus();
  });
  $("inspect-shortlist").addEventListener("click", () => toggleShortlist(selected));
  $("inspector").addEventListener("close", () => {
    if (opener?.checkVisibility()) opener.focus();
    else $("shortlist-only").focus();
  });
  for (const img of document.querySelectorAll('img[src*="d2-fan-balanced"]')) {
    img.addEventListener("error", () => reportImageError(img), { once: true });
    if (img.complete && img.naturalWidth === 0) reportImageError(img);
  }
  $("controls").disabled = false;
  updateFilters();
})();
