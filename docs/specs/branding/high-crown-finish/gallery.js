(() => {
  "use strict";
  const button = document.getElementById("background");
  button.hidden = false;
  button.addEventListener("click", () => {
    button.setAttribute("aria-pressed", String(document.body.classList.toggle("dark")));
  });
  const failures = new Set();
  function report(img) {
    failures.add(img.getAttribute("src"));
    const message = document.getElementById("image-error");
    message.hidden = false;
    message.textContent = `Cannot load SVG: ${[...failures].join(", ")}. Keep the study folders together, then reload.`;
  }
  for (const img of document.images) {
    img.addEventListener("error", () => report(img), { once: true });
    if (img.complete && img.naturalWidth === 0) report(img);
  }
})();
