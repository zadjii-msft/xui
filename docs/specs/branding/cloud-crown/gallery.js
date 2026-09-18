(() => {
  "use strict";
  const $ = id => document.getElementById(id);
  const expectedFiles = [
    "c01-cloud-crown.svg", "c02-rounder-billows.svg", "c03-high-crown.svg",
    "c04-broad-cloud.svg", "c05-compact-cloud.svg", "c06-extra-scallops.svg"
  ];
  const raw = window.cloudCrownStudies;
  const text = value => typeof value === "string" && value.trim().length > 0;
  const valid = raw && Array.isArray(raw.variants) && raw.variants.length === 6 &&
    raw.variants.every((item, index) => item && item.id === `c${String(index + 1).padStart(2, "0")}` &&
      item.label === item.id.toUpperCase() && item.file === expectedFiles[index] && text(item.name) && text(item.note));
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
    $("load-error").textContent = "Cannot load the six Cloud Crown options. Keep a valid studies.js beside this page, then reload.";
    $("results").textContent = "Study collection unavailable.";
    return;
  }

  const anchor = raw.variants[0];

  function toggleBackground() {
    const dark = document.body.classList.toggle("dark");
    for (const id of ["background", "inspect-background"]) $(id).setAttribute("aria-pressed", String(dark));
  }

  function inspect(study, trigger) {
    opener = trigger;
    $("inspect-title").textContent = `${study.label} / ${study.name}`;
    $("inspect-caption").textContent = `${study.label} / ${study.name}`;
    $("inspect-note").textContent = `${study.note} Both drawings use the same Softling face and six Enamel mane colors.`;
    $("inspect-error").hidden = true;
    $("anchor-preview").replaceChildren(image(anchor, 280));
    $("large-preview").replaceChildren(image(study, 280));
    for (const size of [24, 48]) $(`sample-${size}`).replaceChildren(image(study, size));
    $("download").href = study.file;
    $("download").download = study.file;
    $("download").textContent = `Download ${study.label} SVG`;
    $("inspector").showModal();
  }

  for (const study of raw.variants) {
    const card = element("article", study === anchor ? "card anchor-card" : "card");
    card.dataset.study = study.id;
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
    const title = element("h3", "", `${study.label} / ${study.name}`);
    title.id = `title-${study.id}`;
    const actions = element("div", "card-actions");
    const download = element("a", "", "SVG");
    download.href = study.file;
    download.download = study.file;
    download.setAttribute("aria-label", `Download ${study.label} SVG`);
    actions.append(element("span", "", study === anchor ? "Recolored M10 anchor" : "Compare with C01"), download);
    body.append(title, element("p", "", study.note), actions);
    card.append(button, body);
    $("gallery").append(card);
  }

  $("background").addEventListener("click", toggleBackground);
  $("inspect-background").addEventListener("click", toggleBackground);
  $("inspector").addEventListener("close", () => {
    if (opener?.isConnected) opener.focus();
    else $("background").focus();
  });
  $("background").disabled = false;
  $("results").textContent = "All six options · C01 is the recolored M10 anchor";
})();
