// Match published asset names, never constructing links to files that may not exist.
const downloadGroups = [
  { title: 'Windows', builds: [
    { label: 'Windows x64', detail: 'Installer · 64-bit Intel / AMD', pattern: /^PohonMC-Windows-MSVC-Setup-.+\.exe$/i },
    { label: 'Windows ARM64', detail: 'Installer · ARM processors', pattern: /^PohonMC-Windows-MSVC-arm64-Setup-.+\.exe$/i }
  ] },
  { title: 'macOS', builds: [
    { label: 'macOS universal', detail: 'Disk image · Apple silicon & Intel', pattern: /^PohonMC-macOS-.+\.dmg$/i }
  ] },
  { title: 'Linux', builds: [
    { label: 'Linux x86-64', detail: 'AppImage · 64-bit Intel / AMD', pattern: /^PohonMC-Linux-x86_64\.AppImage$/i },
    { label: 'Linux ARM64', detail: 'AppImage · ARM processors', pattern: /^PohonMC-Linux-aarch64\.AppImage$/i }
  ] },
  { title: 'Source', builds: [
    { label: 'Source code', detail: 'Source archive · tar.gz', pattern: /^PohonMC-[\d][\w.+-]*\.tar\.gz$/i,
      fallback: 'https://github.com/kodin00/PohonMC/archive/refs/heads/develop.tar.gz' }
  ] }
];

function renderDownloads(assets) {
  const list = document.getElementById('download-list');
  for (const group of downloadGroups) {
    const card = document.createElement('div');
    card.className = 'download-card';
    const heading = document.createElement('h3');
    heading.textContent = group.title;
    card.append(heading);
    for (const build of group.builds) {
      const asset = assets.find(item =>
        item.state === 'uploaded' &&
        build.pattern.test(item.name) &&
        typeof item.browser_download_url === 'string' &&
        item.browser_download_url.startsWith('https://github.com/kodin00/PohonMC/releases/download/')
      );
      const url = asset?.browser_download_url ?? build.fallback;
      const row = document.createElement('div');
      row.className = 'download-build';
      if (url) {
        const link = document.createElement('a');
        link.className = 'download-button';
        link.href = url;
        link.textContent = `${build.label} ↓`;
        row.append(link);
      } else {
        const unavailable = document.createElement('span');
        unavailable.className = 'download-unavailable';
        unavailable.textContent = `${build.label} unavailable`;
        row.append(unavailable);
      }
      const detail = document.createElement('small');
      detail.textContent = !asset && build.fallback ? 'Development source · tar.gz' : build.detail;
      row.append(detail);
      card.append(row);
    }
    list.append(card);
  }
}

async function loadDownloads() {
  const status = document.getElementById('download-status');
  let assets = [];
  try {
    const response = await fetch('https://api.github.com/repos/kodin00/PohonMC/releases/latest', {
      headers: { Accept: 'application/vnd.github+json' }
    });
    if (response.status === 404) {
      status.textContent = 'Installer downloads will appear when the first release is published.';
    } else {
      if (!response.ok) throw new Error('Release lookup failed');
      const release = await response.json();
      if (!Array.isArray(release.assets) || typeof release.tag_name !== 'string') throw new Error('Invalid release data');
      status.textContent = `Latest stable release: ${release.tag_name}`;
      assets = release.assets;
    }
  } catch {
    status.textContent = 'Could not load installer downloads. Please try again later.';
  }
  renderDownloads(assets);
}

loadDownloads();
