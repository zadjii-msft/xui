(() => {
  "use strict";
  const $ = id => document.getElementById(id);
  const reference = { label: "C03", name: "High crown reference", file: "../cloud-crown/c03-high-crown.svg" };
  const families = ["Skyward", "Wide & low", "Botanical", "Sculptural", "Offbeat", "Face & frame"];
  const raw = window.cloudCrownLab;
  const text = value => typeof value === "string" && value.trim().length > 0;
  const valid = raw && Array.isArray(raw.variants) && raw.variants.length === 30 &&
    raw.variants.every((item, index) => item && item.id === `b${String(index + 1).padStart(2, "0")}` &&
      item.label === item.id.toUpperCase() && /^b\d{2}-[a-z0-9-]+\.svg$/.test(item.file) &&
      text(item.name) && text(item.note) && families.includes(item.family)) &&
    new Set(raw.variants.map(item => item.file)).size === 30 &&
    families.every(family => raw.variants.filter(item => item.family === family).length === 5);
  const failures = new Set();
  const cards = [];
  const rows = [];
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

  const referenceImage = $("original-reference");
  referenceImage.addEventListener("error", () => reportImageError(referenceImage), { once: true });
  if (referenceImage.complete && referenceImage.naturalWidth === 0) reportImageError(referenceImage);

  if (!valid) {
    $("load-error").hidden = false;
    $("load-error").textContent = "Cannot load the 30 Cloud Crown ideas. Keep a valid studies.js beside this page, then reload.";
    $("results").textContent = "Study collection unavailable.";
    return;
  }

  function updateFilters() {
    const query = $("search").value.trim().toLowerCase();
    const number = query.match(/^b?0*(\d+)$/);
    let count = 0;
    for (const { card, study } of cards) {
      const matchesSearch = number ? Number(study.id.slice(1)) === Number(number[1]) :
        query.split(/\s+/).every(term => `${study.label} ${study.name} ${study.family} ${study.note}`.toLowerCase().includes(term));
      card.hidden = !matchesSearch || Boolean($("family").value && $("family").value !== study.family);
      if (!card.hidden) count++;
    }
    for (const row of rows) row.hidden = !cards.some(({ card }) => card.closest(".family-row") === row && !card.hidden);
    $("results").textContent = `${count} of 30 ideas`;
    $("empty").hidden = count > 0;
  }

  function toggleBackground() {
    const dark = document.body.classList.toggle("dark");
    for (const id of ["background", "inspect-background"]) $(id).setAttribute("aria-pressed", String(dark));
  }

  function inspect(study, trigger) {
    opener = trigger;
    $("inspect-title").textContent = `${study.label} / ${study.name}`;
    $("inspect-caption").textContent = `${study.label} / ${study.family}`;
    $("inspect-note").textContent = study.note;
    $("inspect-error").hidden = true;
    $("reference-preview").replaceChildren(image(reference, 280));
    $("large-preview").replaceChildren(image(study, 280));
    for (const size of [24, 48]) $(`sample-${size}`).replaceChildren(image(study, size));
    $("download").href = study.file;
    $("download").download = study.file;
    $("download").textContent = `Download ${study.label} SVG`;
    $("inspector").showModal();
  }

  for (const [index, family] of families.entries()) {
    const option = element("option", "", family);
    option.value = family;
    $("family").append(option);
    const row = element("section", "family-row");
    row.dataset.family = family;
    row.setAttribute("aria-labelledby", `family-${index}`);
    const heading = element("h2", "", family);
    heading.id = `family-${index}`;
    const grid = element("div", "lab-grid");
    for (const study of raw.variants.filter(item => item.family === family)) {
      const card = element("article", "card");
      card.dataset.study = study.id;
      card.setAttribute("aria-labelledby", `title-${study.id}`);
      const button = element("button", "inspect");
      button.type = "button";
      button.setAttribute("aria-label", `Inspect ${study.label}: ${study.name}`);
      button.setAttribute("aria-haspopup", "dialog");
      button.setAttribute("aria-controls", "inspector");
      const board = element("span", "artboard");
      board.append(image(study, 180));
      button.append(board);
      button.addEventListener("click", () => inspect(study, button));
      const body = element("div", "card-body");
      const title = element("h3", "", `${study.label} / ${study.name}`);
      title.id = `title-${study.id}`;
      const actions = element("div", "card-actions");
      const download = element("a", "", "SVG");
      download.href = study.file;
      download.download = study.file;
      download.setAttribute("aria-label", `Download ${study.label} SVG`);
      actions.append(element("span", "", "Compare with C03"), download);
      body.append(title, element("p", "", study.note), actions);
      card.append(button, body);
      grid.append(card);
      cards.push({ card, study });
    }
    row.append(heading, grid);
    $("gallery").append(row);
    rows.push(row);
  }

  $("search").addEventListener("input", updateFilters);
  $("family").addEventListener("change", updateFilters);
  $("reset").addEventListener("click", () => {
    $("search").value = "";
    $("family").value = "";
    updateFilters();
  });
  $("background").addEventListener("click", toggleBackground);
  $("inspect-background").addEventListener("click", toggleBackground);
  $("inspector").addEventListener("close", () => {
    if (opener?.isConnected && !opener.closest(".card").hidden) opener.focus();
    else $("search").focus();
  });
  $("controls").disabled = false;
  updateFilters();
})();
