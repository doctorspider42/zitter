// Service worker of testdata/notification_probe.html. It reports clicks and
// closes of persistent notifications back to the open lab pages.
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));

async function report(message) {
  const pages = await self.clients.matchAll({ type: 'window', includeUncontrolled: true });
  for (const page of pages)
    page.postMessage(message);
  return pages;
}

self.addEventListener('notificationclick', (event) => {
  const label = event.notification.data?.label;
  event.notification.close();
  event.waitUntil((async () => {
    const pages = await report({ type: 'click', label, action: event.action });
    if (pages.length)
      await pages[0].focus();
    else
      await self.clients.openWindow('notification_probe.html');
  })());
});

self.addEventListener('notificationclose', (event) => {
  event.waitUntil(report({ type: 'close', label: event.notification.data?.label }));
});
