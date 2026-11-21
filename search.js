const filter = document.querySelector("#filter");
if (filter) {
  const cards = Array.from(document.querySelectorAll(".searchable"), card => ({
    card, text: card.textContent.toLocaleLowerCase()
  }));
  const matches = document.querySelector("#matches");
  const refresh = () => {
    const terms = filter.value.toLocaleLowerCase().trim().split(/\s+/).filter(Boolean);
    let count = 0;
    for (const {card, text} of cards) {
      card.hidden = !terms.every(term => text.includes(term));
      if (!card.hidden) count++;
    }
    matches.textContent = terms.length ? `${count} ${count === 1 ? "match" : "matches"}` : "";
  };
  filter.addEventListener("input", refresh);
  refresh();
}
if (location.hash) {
  const section = document.getElementById(decodeURIComponent(location.hash.slice(1)));
  if (section instanceof HTMLDetailsElement) section.open = true;
}
