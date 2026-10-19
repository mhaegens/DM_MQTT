// The library stays offline. Only the selected twin opens a telemetry session.
const make = (tag, className, value) => {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (value !== undefined) node.textContent = value;
  return node;
};
function twinCard(twin) {
  const card = make('article', 'twin-card');
  card.dataset.twin = twin.id;
  const preview = make('a', 'twin-preview');
  preview.href = twin.url;
  preview.setAttribute('aria-label', `Open ${twin.name} digital twin`);
  const image = make('img');
  image.src = twin.preview;
  image.alt = `${twin.name} 3D floor with brewery equipment and storage vessels`;
  image.width = 1000; image.height = 620;
  const previewBadge = make('span', 'preview-badge', 'INTERACTIVE 3D');
  const previewCaption = make('span', 'preview-caption', 'Grain. Craft. Glass.');
  preview.append(image, previewBadge, previewCaption);
  const body = make('div', 'twin-body');
  const category = make('p', 'twin-category', twin.category);
  const title = make('h3');
  const titleLink = make('a', '', twin.name); titleLink.href = twin.url; title.append(titleLink);
  const description = make('p', 'twin-description', twin.description);
  const areas = make('p', 'twin-areas', twin.areas.join('  /  '));
  const stats = make('dl', 'twin-stats');
  for (const [value, label] of [[twin.machines, 'Machines'], [twin.vessels, 'Vessels'], [twin.signals, 'Signals']]) {
    const item = make('div'); item.append(make('dd', '', value), make('dt', '', label)); stats.append(item);
  }
  const profile = make('p', 'twin-profile', twin.profile);
  const actions = make('div', 'twin-actions');
  const open = make('a', 'action primary', 'Open twin'); open.href = twin.url;
  open.append(make('span', '', '↗')); open.setAttribute('aria-label', `Open ${twin.name}`);
  const demo = make('a', 'action secondary', 'Explore demo'); demo.href = twin.demoUrl;
  demo.setAttribute('aria-label', `Explore ${twin.name} demo`);
  actions.append(open, demo); body.append(category, title, description, areas, stats, profile, actions);
  card.append(preview, body); return card;
}
async function load() {
  const response = await fetch('./catalog.json', {cache:'no-cache'});
  if (!response.ok) throw Error('Catalog unavailable');
  const twins = await response.json();
  const list = document.getElementById('twin-list');
  list.replaceChildren(...twins.map(twinCard));
  document.getElementById('twin-count').textContent = `${twins.length} ${twins.length === 1 ? 'twin' : 'twins'}`;
  list.setAttribute('aria-busy', 'false');
}
load().catch(() => {
  document.getElementById('twin-list').setAttribute('aria-busy', 'false');
  document.getElementById('catalog-error').hidden = false;
  document.getElementById('twin-count').textContent = 'Unavailable';
});
