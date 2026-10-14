(() => {
  'use strict';
  const pages = {
    '/dashboard/': ['monitor', 'MQTT Monitor', 'Follow every signal.', 'Inspect live messages, publish test values, and measure the broker round trip.'],
    '/shopfloor/': ['shopfloor', 'Shop Floor', 'Your plant, in real time.', 'Machines, process tanks, and operator activity in one live view.'],
    '/config-editor/': ['studio', 'Config Studio', 'Build your next demo.', 'Design machines, tanks, and operator badges with a live device preview.'],
    '/editor/': ['editor', 'JSON Editor', 'Make the configuration yours.', 'Edit signals and simulation profiles, then export or upload your device configuration.']
  };
  const path = location.pathname.endsWith('/') ? location.pathname : location.pathname.replace(/index\.html$/, '');
  const info = pages[path];
  if (!info) return;
  document.body.dataset.workbench = info[0];
  const nav = document.createElement('nav');
  nav.className = 'workbench-nav'; nav.setAttribute('aria-label', 'Workbench');
  nav.innerHTML = '<a class="workbench-brand" href="/dashboard/"><img class="workbench-logo" src="/assets/sap-logo.svg" alt="SAP" width="52" height="26">Digital Manufacturing<span style="color:#667b8f;font-weight:400"> / Demo Workbench</span></a><div class="workbench-links">' + Object.entries(pages).map(([url, item]) => `<a href="${url}" ${url === path ? 'aria-current="page"' : ''}>${item[1]}</a>`).join('') + '</div>';
  const intro = document.createElement('div'); intro.className = 'workbench-intro';
  intro.innerHTML = `<div class="workbench-kicker">Connected shop floor · ${info[1]}</div><h1>${info[2]}</h1><p>${info[3]}</p>`;
  document.body.prepend(nav, intro);
  window.dispatchEvent(new Event("resize"));
})();
