/* Browser counterpart of firmware/main/ui/ui_tank_shape.c.
 * One clipped vessel shape keeps the liquid inside its shoulders and cone.
 * A fixed viewBox preserves the ESP proportions at every browser size.
 */
(() => {
  'use strict';
  const W = 200, H = 340, left = 14, right = 186, join = 218, bottom = 302;
  const shape = 'M48 12H152A34 34 0 0 1 186 46V218L109 302H91L14 218V46A34 34 0 0 1 48 12Z';
  let sequence = 0;
  const clamp = value => Number.isFinite(Number(value)) ? Math.max(0, Math.min(100, Number(value))) : 0;
  const levelY = level => 12 + (H - 50) * (100 - level) / 100;
  function rightAt(level) {
    const y = levelY(level);
    if (y > join) return W / 2 + 9 + (right - W / 2 - 9) * (bottom - y) / (bottom - join);
    if (y < 46) return right - 34 + Math.sqrt(34 * 34 - (46 - y) ** 2);
    return right;
  }
  function status(value) {
    const level = clamp(value);
    return level < 15 ? {label:'Critical', tone:'err'} : level < 40 ? {label:'Low level', tone:'warn'} : {label:'Operational', tone:'ok'};
  }
  function color(value) {
    const level = clamp(value);
    return level < 15 ? '#BB0000' : level < 40 ? '#E76500' : level < 75 ? '#0070F2' : '#0092D1';
  }
  function fillAttributes(value, fillColor) {
    const level = clamp(value), y = levelY(level), edge = rightAt(level);
    const labelY = Math.max(70, Math.min(join - 24, y + 38));
    return {level, y, edge, labelY, labelColor:labelY - 10 >= y ? '#FFFFFF' : '#4A6080', color:/^#[0-9a-f]{6}$/i.test(fillColor || '') ? fillColor : color(level)};
  }
  function markup(value = 0, fillColor) {
    const clip = 'vessel-clip-' + (++sequence), a = fillAttributes(value, fillColor);
    const ticks = [25,50,75].map(p => `<path d="M${rightAt(p)-11} ${levelY(p)}h8"/>`).join('');
    return `<svg class="vessel-svg" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" role="img" aria-label="Fill level ${Math.round(a.level)}%">
      <defs><clipPath id="${clip}"><path d="${shape}"/></clipPath></defs>
      <g fill="#74869B"><rect x="45" y="230" width="6" height="106" rx="2"/><rect x="149" y="230" width="6" height="106" rx="2"/>
        <rect x="38" y="334" width="20" height="4" rx="2"/><rect x="142" y="334" width="20" height="4" rx="2"/></g>
      <path d="${shape}" fill="#EDF2F7"/>
      <g clip-path="url(#${clip})">
        <rect class="vessel-liquid" x="${left}" y="${a.y}" width="${right-left}" height="${bottom-a.y}" fill="${a.color}"/>
        <path d="M25 52V206" stroke="#FFFFFF" stroke-opacity=".2" stroke-width="5" stroke-linecap="round"/>
        <path class="vessel-surface" d="M${W-a.edge+3} ${a.y+1}H${a.edge-3}" fill="none" stroke="#FFFFFF" stroke-opacity=".4" stroke-width="2"/>
      </g>
      <path d="${shape}" fill="none" stroke="#7D8DA2" stroke-width="2" stroke-linejoin="round"/>
      <path d="M15 ${join}H185" stroke="#74869B" stroke-opacity=".4"/>
      <g fill="#74869B"><rect x="87" y="6" width="26" height="6" rx="3"/><rect x="92" y="302" width="16" height="15" rx="2"/>
        <rect x="88" y="312" width="24" height="4" rx="2"/></g>
      <g fill="none" stroke="#74869B" stroke-opacity=".7">${ticks}</g>
      <text class="vessel-level" x="100" y="${a.labelY}" text-anchor="middle" fill="${a.labelColor}" font-family="Arial,sans-serif" font-size="29" font-weight="600">${Math.round(a.level)}%</text>
    </svg>`;
  }
  function update(svg, value, fillColor) {
    if (!svg) return;
    const a = fillAttributes(value, fillColor);
    svg.setAttribute('aria-label', `Fill level ${Math.round(a.level)}%`);
    const liquid = svg.querySelector('.vessel-liquid');
    liquid.setAttribute('y', a.y);
    liquid.setAttribute('height', bottom - a.y);
    liquid.setAttribute('fill', a.color);
    svg.querySelector('.vessel-surface').setAttribute('d', `M${W-a.edge+3} ${a.y+1}H${a.edge-3}`);
    const label = svg.querySelector('.vessel-level');
    label.textContent = Math.round(a.level) + '%';
    label.setAttribute('y', a.labelY);
    label.setAttribute('fill', a.labelColor);
  }
  window.dmTank = Object.freeze({markup, update, status});
})();
