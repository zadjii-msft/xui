(() => {
  "use strict";
  const $ = id => document.getElementById(id);
  const original = { label: "S06", name: "Original Softling", file: "../solar-d2-styles/06-softling.svg" };
  const expectedContours = ["plush", "halo", "cloud", "wave"];
  const expectedJoins = ["narrow", "joined", "lined", "wavy"];
  const raw = window.softlingStudies;
  const text = value => typeof value === "string" && value.trim().length > 0;
  const valid = raw && Array.isArray(raw.contours) && Array.isArray(raw.joins) && Array.isArray(raw.variants) &&
    raw.contours.length === 4 && raw.joins.length === 4 && raw.variants.length === 16 &&
    raw.contours.every((item, index) => item && item.id === expectedContours[index] &&
      item.letter === "ABCD"[index] && text(item.name) && text(item.note)) &&
    raw.joins.every((item, index) => item && item.id === expectedJoins[index] &&
      item.column === index + 1 && text(item.name) && text(item.note)) &&
    raw.variants.every((item, index) => item && item.id === `m${String(index + 1).padStart(2, "0")}` &&
      item.label === item.id.toUpperCase() && item.contour === expectedContours[Math.floor(index / 4)] &&
      item.join === expectedJoins[index % 4] && /^m\d{2}-[a-z0-9-]+\.svg$/.test(item.file) && text(item.name)) &&
    new Set(raw.variants.map(item => item.file)).size === 16;
  const failures = new Set();
  let opener = null;

  function element(tag, className, content) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (content !== undefined) node.textContent = content;
    return node;
  }

  function reportImageError(img) {
    if (img.dataset.failed) return;
    img.dataset.failed = "true";
    const file = img.getAttribute("src");
    failures.add(file);
    $("image-error").hidden = false;
    $("image-error").textContent = `Cannot load SVG: ${[...failures].join(", ")}. Keep the study folders together, then reload.`;
    if (img.closest("dialog")) {
      $("inspect-error").hidden = false;
      $("inspect-error").textContent = `Cannot load ${file}. Keep the study folders together, then reload.`;
    }
    img.hidden = true;
    img.parentElement.append(element("span", "image-failure", `SVG unavailable: ${file}`));
  }

  function image(study, size) {
    const img = element("img");
    img.width = size;
    img.height = size;
    img.alt = `${study.label} / ${study.name}`;
    img.addEventListener("error", () => reportImageError(img), { once: true });
    img.src = study.file;
    return img;
  }

  const reference = $("original-reference");
  reference.addEventListener("error", () => reportImageError(reference), { once: true });
  if (reference.complete && reference.naturalWidth === 0) reportImageError(reference);

  if (!valid) {
    $("load-error").hidden = false;
    $("load-error").textContent = "Cannot load the 4 × 4 Softling study. Keep a valid studies.js beside this page, then reload.";
    $("results").textContent = "Study collection unavailable.";
    return;
  }

  function toggleBackground() {
    const dark = document.body.classList.toggle("dark");
    for (const id of ["background", "inspect-background"]) $(id).setAttribute("aria-pressed", String(dark));
  }

  function inspect(study, trigger) {
    opener = trigger;
    const contour = raw.contours.find(item => item.id === study.contour);
    const join = raw.joins.find(item => item.id === study.join);
    $("inspect-title").textContent = `${study.label} / ${study.name}`;
    $("inspect-caption").textContent = `${study.label} / ${contour.name} / ${join.name}`;
    $("inspect-note").textContent = `${contour.note} ${join.note} Same face and pastel colors as S06.`;
    $("inspect-error").hidden = true;
    $("original-preview").replaceChildren(image(original, 280));
    $("large-preview").replaceChildren(image(study, 280));
    for (const size of [24, 48]) $(`sample-${size}`).replaceChildren(image(study, size));
    $("download").href = study.file;
    $("download").download = study.file;
    $("download").textContent = `Download ${study.label} SVG`;
    $("inspector").showModal();
  }

  for (const join of raw.joins) {
    const heading = element("div", "column-heading");
    heading.append(element("strong", "", `${join.column} / ${join.name}`), element("p", "", join.note));
    $("column-headings").append(heading);
  }

  for (const contour of raw.contours) {
    const row = element("section", "contour-row");
    row.dataset.contour = contour.id;
    row.setAttribute("aria-labelledby", `row-${contour.id}`);
    const heading = element("div", "row-heading");
    const title = element("h2", "", `${contour.letter} / ${contour.name}`);
    title.id = `row-${contour.id}`;
    heading.append(title, element("p", "", contour.note));
    const grid = element("div", "row-grid");
    for (const study of raw.variants.filter(item => item.contour === contour.id)) {
      const join = raw.joins.find(item => item.id === study.join);
      const card = element("article", "card");
      card.dataset.study = study.id;
      card.dataset.join = study.join;
      card.setAttribute("aria-labelledby", `title-${study.id}`);
      const button = element("button", "inspect");
      button.type = "button";
      button.setAttribute("aria-label", `Inspect ${study.label}: ${study.name}`);
      button.setAttribute("aria-haspopup", "dialog");
      button.setAttribute("aria-controls", "inspector");
      const board = element("span", "artboard");
      board.append(image(study, 210));
      button.append(board);
      button.addEventListener("click", () => inspect(study, button));
      const body = element("div", "card-body");
      const cardTitle = element("h3", "", `${study.label} / ${join.name}`);
      cardTitle.id = `title-${study.id}`;
      const actions = element("div", "card-actions");
      const download = element("a", "", "SVG");
      download.href = study.file;
      download.download = study.file;
      download.setAttribute("aria-label", `Download ${study.label} SVG`);
      actions.append(element("span", "", `${contour.letter}${join.column} · ${contour.name}`), download);
      body.append(cardTitle, actions);
      card.append(button, body);
      grid.append(card);
    }
    row.append(heading, grid);
    $("gallery").append(row);
  }

  $("background").addEventListener("click", toggleBackground);
  $("inspect-background").addEventListener("click", toggleBackground);
  $("inspector").addEventListener("close", () => {
    if (opener?.isConnected) opener.focus();
    else $("background").focus();
  });
  $("background").disabled = false;
  $("results").textContent = "All 16 options · Rows: outer contours · Columns: seam treatments";
})();
