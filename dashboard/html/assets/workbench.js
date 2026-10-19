(() => {
  'use strict';
  const pages = {
    '/dashboard/': ['monitor', 'MQTT Monitor'],
    '/shopfloor/': ['shopfloor', 'Asset Cards'],
    '/digital-twins/': ['twins', 'Digital Twins'],
    '/config-editor/': ['studio', 'Config Studio']
  };
  const path = location.pathname.replace(/\/index\.html$/, '/').replace(/\/?$/, '/');
  const currentPage = path === '/twin/' ? '/digital-twins/' : path;
  const info = path === '/twin/' ? ['twin', 'Brewery Twin'] : pages[path];
  if (!info) return;
  document.body.dataset.workbench = info[0];
  const nav = document.createElement('nav');
  nav.className = 'workbench-nav'; nav.setAttribute('aria-label', 'Workbench');
  nav.innerHTML = '<a class="workbench-brand" href="/dashboard/"><img class="workbench-logo" src="/assets/sap-logo.svg?v=20261015.1" alt="SAP" width="52" height="26"><span>Digital Manufacturing</span></a><div class="workbench-links">' + Object.entries(pages).map(([url, item]) => `<a href="${url}" ${url === currentPage ? 'aria-current="page"' : ''}>${item[1]}</a>`).join('') + '</div>';
  // Keep the live status node: MQTT callbacks retain references to it.
  const status = document.getElementById('status-badge') || document.getElementById('conn-badge');
  if (status) {
    const slot = document.createElement('div');
    slot.className = 'workbench-status';
    slot.append(status);
    nav.append(slot);
  }
  const title = document.createElement('h1');
  title.className = 'visually-hidden';
  title.textContent = info[1];
  document.body.prepend(nav, title);
  window.dispatchEvent(new Event("resize"));
})();
