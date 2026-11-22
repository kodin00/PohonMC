const repository = 'kodin00/PohonMC';
const api = `https://api.github.com/repos/${repository}`;

const downloadGroups = [
  { title: 'Windows', builds: [
    { label: 'Windows x64', format: 'Installer', platform: '64-bit Intel / AMD',
      release: /^PohonMC-Windows-MSVC-Setup-.+\.exe$/i, development: /^PohonMC-Windows-MSVC-Setup-[\da-f]+-Debug$/i },
    { label: 'Windows ARM64', format: 'Installer', platform: 'ARM processors',
      release: /^PohonMC-Windows-MSVC-arm64-Setup-.+\.exe$/i, development: /^PohonMC-Windows-MSVC-arm64-Setup-[\da-f]+-Debug$/i }
  ] },
  { title: 'macOS', builds: [
    { label: 'macOS universal', format: 'Disk image', platform: 'Apple silicon & Intel',
      release: /^PohonMC-macOS-.+\.dmg$/i, development: /^PohonMC-macOS-[\da-f]+-Debug\.dmg$/i }
  ] },
  { title: 'Linux', builds: [
    { label: 'Linux x86-64', format: 'AppImage', platform: '64-bit Intel / AMD',
      release: /^PohonMC-Linux-x86_64\.AppImage$/i, development: /^PohonMC-Linux-[\da-f]+-Debug-x86_64\.AppImage$/i },
    { label: 'Linux ARM64', format: 'AppImage', platform: 'ARM processors',
      release: /^PohonMC-Linux-aarch64\.AppImage$/i, development: /^PohonMC-Linux-[\da-f]+-Debug-aarch64\.AppImage$/i }
  ] },
  { title: 'Source', builds: [
    { label: 'Source code', format: 'Source archive', platform: 'tar.gz', release: /^PohonMC-[\d][\w.+-]*\.tar\.gz$/i,
      fallback: `https://github.com/${repository}/archive/refs/heads/develop.tar.gz` }
  ] }
];

function renderDownloads(releaseAssets, developmentAssets) {
  const list = document.getElementById('download-list');
  for (const group of downloadGroups) {
    const card = document.createElement('div');
    card.className = 'download-card';
    const heading = document.createElement('h3');
    heading.textContent = group.title;
    card.append(heading);
    for (const build of group.builds) {
      const release = releaseAssets.find(item => item.state === 'uploaded' && build.release.test(item.name) &&
        typeof item.browser_download_url === 'string' &&
        item.browser_download_url.startsWith(`https://github.com/${repository}/releases/download/`)
      );
      const development = developmentAssets.find(item => build.development?.test(item.name));
      const url = release?.browser_download_url ?? development?.url ?? build.fallback;
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
      const format = development ? `${build.format} in ZIP` : build.format;
      detail.textContent = !release && build.fallback ? 'Development source · tar.gz' : `${format} · ${build.platform}`;
      row.append(detail);
      card.append(row);
    }
    list.append(card);
  }
}

async function loadDevelopmentAssets() {
  const runs = await fetch(`${api}/actions/workflows/build.yml/runs?branch=develop&event=push&status=success&per_page=1`);
  if (!runs.ok) throw new Error('Development build lookup failed');
  const { workflow_runs: [latest] } = await runs.json();
  if (!Number.isSafeInteger(latest?.id) || !Number.isSafeInteger(latest.check_suite_id)) return [];

  const response = await fetch(`${api}/actions/runs/${latest.id}/artifacts?per_page=100`);
  if (!response.ok) throw new Error('Development download lookup failed');
  const { artifacts } = await response.json();
  // Nightly.link gives anonymous visitors direct ZIP downloads for public GitHub Actions artifacts.
  return artifacts.filter(item => !item.expired && Number.isSafeInteger(item.id)).map(item => ({
    name: item.name,
    url: `https://nightly.link/${repository}/suites/${latest.check_suite_id}/artifacts/${item.id}`
  }));
}

async function loadDownloads() {
  const status = document.getElementById('download-status');
  let releaseAssets = [];
  let developmentAssets = [];
  try {
    const response = await fetch(`${api}/releases/latest`, {
      headers: { Accept: 'application/vnd.github+json' }
    });
    if (response.status === 404) {
      developmentAssets = await loadDevelopmentAssets();
      const hasInstallers = downloadGroups.some(group => group.builds.some(build =>
        developmentAssets.some(item => build.development?.test(item.name))
      ));
      status.textContent = hasInstallers ? 'You’re viewing the latest development builds.' :
        'Installer downloads will appear when the first release is published.';
    } else {
      if (!response.ok) throw new Error('Release lookup failed');
      const release = await response.json();
      if (!Array.isArray(release.assets) || typeof release.tag_name !== 'string') throw new Error('Invalid release data');
      status.textContent = `Latest stable release: ${release.tag_name}`;
      releaseAssets = release.assets;
    }
  } catch {
    status.textContent = 'Could not load installer downloads. Please try again later.';
  }
  renderDownloads(releaseAssets, developmentAssets);
}

loadDownloads();
