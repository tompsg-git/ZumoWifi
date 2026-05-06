'use strict';

let currentPath = '/';
let logPaused   = false;
let logAutoScroll = true;
let logNextIndex  = 0;
let logPollTimer  = null;

const TEXT_EXTS = new Set([
  'txt','xml','gpx','json','csv','nmea','log','ini','cfg','kml',
  'htm','html','js','css','md','yaml','yml','ov2','wpr'
]);

function isTextFile(name) {
    return TEXT_EXTS.has(name.split('.').pop().toLowerCase());
}

// ── Tab-Navigation ───────────────────────────────────────────────────

document.querySelectorAll('.tab').forEach(btn => {
    btn.addEventListener('click', () => {
        document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
        document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
        btn.classList.add('active');
        document.getElementById('tab-' + btn.dataset.tab).classList.add('active');
        if (btn.dataset.tab === 'log') startLogPolling();
        else stopLogPolling();
    });
});

// ── Status-Polling ───────────────────────────────────────────────────

async function fetchStatus() {
    try {
        const [statusRes, usbRes] = await Promise.all([
            fetch('/api/status'),
            fetch('/api/usb/status')
        ]);
        const s = await statusRes.json();
        const u = await usbRes.json();

        const badge = document.getElementById('status-badge');
        if (u.mounted) {
            badge.textContent = u.device || 'Stick bereit';
            badge.className   = 'badge online';
        } else {
            badge.textContent = 'Kein Stick';
            badge.className   = 'badge offline';
        }

        document.getElementById('wifi-mode').textContent  = s.wifi_mode;
        document.getElementById('wifi-ssid').textContent  = s.wifi_ssid;
        document.getElementById('wifi-ip').textContent    = s.wifi_ip;
        document.getElementById('info-device').textContent = u.device || '–';
        document.getElementById('info-heap').textContent  = fmtBytes(s.heap_free);
        document.getElementById('info-uptime').textContent = fmtUptime(s.uptime);

        // Disk-Bar
        const diskBar  = document.getElementById('disk-bar');
        const diskFill = document.getElementById('disk-fill');
        const diskLabel = document.getElementById('disk-label');
        if (u.mounted && u.disk_total) {
            const pct = Math.round((u.disk_used / u.disk_total) * 100);
            diskBar.hidden = false;
            diskFill.style.width = pct + '%';
            diskFill.className = 'disk-fill' + (pct > 85 ? ' warn' : '');
            diskLabel.textContent =
                fmtBytes(u.disk_free) + ' frei von ' + fmtBytes(u.disk_total);
            document.getElementById('info-disk-total').textContent = fmtBytes(u.disk_total);
            document.getElementById('info-disk-free').textContent  = fmtBytes(u.disk_free);
        } else {
            diskBar.hidden = true;
            document.getElementById('info-disk-total').textContent = '–';
            document.getElementById('info-disk-free').textContent  = '–';
        }
    } catch (_) {}
}

setInterval(fetchStatus, 5000);
fetchStatus();

// ── Log-Viewer ────────────────────────────────────────────────────────

function startLogPolling() {
    if (logPollTimer) return;
    pollLog();
    logPollTimer = setInterval(pollLog, 1500);
}

function stopLogPolling() {
    if (logPollTimer) { clearInterval(logPollTimer); logPollTimer = null; }
}

async function pollLog() {
    if (logPaused) return;
    try {
        const res  = await fetch('/api/log?from=' + logNextIndex);
        const data = await res.json();
        const view = document.getElementById('log-view');

        for (const line of data.lines) {
            const div = document.createElement('div');
            div.className = 'log-line';
            const t = document.createElement('span');
            t.className   = 'log-time';
            t.textContent = fmtMs(line.ms);
            const m = document.createElement('span');
            m.className = 'log-msg';
            m.textContent = line.t;
            if (line.t.includes('[E]')) div.classList.add('log-err');
            else if (line.t.includes('[W]')) div.classList.add('log-warn');
            div.appendChild(t);
            div.appendChild(m);
            view.appendChild(div);
            logNextIndex = line.i + 1;
        }

        // Max. 500 Zeilen im DOM behalten
        while (view.children.length > 500) view.removeChild(view.firstChild);

        if (logAutoScroll && data.lines.length > 0) {
            view.scrollTop = view.scrollHeight;
        }
    } catch (_) {}
}

document.getElementById('btn-log-clear').addEventListener('click', () => {
    document.getElementById('log-view').innerHTML = '';
});

document.getElementById('btn-log-pause').addEventListener('click', e => {
    logPaused = !logPaused;
    e.target.dataset.active = logPaused;
    e.target.textContent = logPaused ? '▶ Weiter' : '⏸ Pause';
});

document.getElementById('btn-log-scroll').addEventListener('click', e => {
    logAutoScroll = !logAutoScroll;
    e.target.dataset.active = logAutoScroll;
    e.target.textContent = logAutoScroll ? '↓ Auto-Scroll' : '↓ Scroll aus';
});

// ── Datei-Browser ────────────────────────────────────────────────────

async function loadFiles(path) {
    currentPath = path || '/';
    document.getElementById('current-path').textContent = currentPath;
    const listEl = document.getElementById('file-list');
    listEl.innerHTML = '<p class="placeholder">Lade …</p>';

    try {
        const res  = await fetch('/api/files?path=' + encodeURIComponent(currentPath));
        if (!res.ok) {
            listEl.innerHTML = '<p class="placeholder">Stick nicht verbunden.</p>';
            return;
        }
        const data = await res.json();
        if (!data.files || !data.files.length) {
            listEl.innerHTML = '<p class="placeholder">Leeres Verzeichnis.</p>';
            return;
        }
        data.files.sort((a,b) => {
            if (a.isDir !== b.isDir) return a.isDir ? -1 : 1;
            return a.name.localeCompare(b.name);
        });
        listEl.innerHTML = '';
        data.files.forEach(f => listEl.appendChild(buildEntry(f)));
    } catch (_) {
        listEl.innerHTML = '<p class="placeholder">Fehler beim Laden.</p>';
    }
}

function buildEntry(f) {
    const div = document.createElement('div');
    div.className = 'file-entry';
    div.dataset.name  = f.name;
    div.dataset.isDir = f.isDir;

    const icon = f.isDir ? '📁' : (isTextFile(f.name) ? '📝' : '📄');
    div.innerHTML = `
        <div class="name">
            <span class="icon">${icon}</span>
            <span>${escHtml(f.name)}</span>
        </div>
        <span class="size">${f.isDir ? '' : fmtBytes(f.size)}</span>
        <div class="actions">
            ${!f.isDir && isTextFile(f.name)
              ? '<button class="edit-btn" title="Bearbeiten">✏</button>' : ''}
            ${!f.isDir
              ? '<button class="dl-btn"  title="Download">↓</button>' : ''}
            <button class="ren-btn" title="Umbenennen">↩</button>
            <button class="del-btn" title="Löschen">🗑</button>
        </div>`;

    const filePath = currentPath === '/' ? '/' + f.name : currentPath + '/' + f.name;

    if (f.isDir) div.addEventListener('click', e => {
        if (e.target.closest('button')) return;
        loadFiles(filePath);
    });

    div.querySelector('.edit-btn')?.addEventListener('click', e => {
        e.stopPropagation(); openEditor(filePath, f.name);
    });
    div.querySelector('.dl-btn')?.addEventListener('click', e => {
        e.stopPropagation();
        window.location.href = '/api/download?path=' + encodeURIComponent(filePath);
    });
    div.querySelector('.ren-btn').addEventListener('click', e => {
        e.stopPropagation(); openRenameDialog(filePath, f.name);
    });
    div.querySelector('.del-btn').addEventListener('click', e => {
        e.stopPropagation();
        if (!confirm('Löschen: ' + f.name + '?')) return;
        fetch('/api/delete', {
            method: 'POST',
            headers: {'Content-Type':'application/x-www-form-urlencoded'},
            body: 'path=' + encodeURIComponent(filePath)
        }).then(() => loadFiles(currentPath));
    });
    return div;
}

document.getElementById('btn-up').addEventListener('click', () => {
    if (currentPath === '/') return;
    const parts = currentPath.split('/').filter(Boolean);
    parts.pop();
    loadFiles('/' + parts.join('/'));
});
document.getElementById('btn-refresh').addEventListener('click', () => loadFiles(currentPath));
document.getElementById('btn-mkdir').addEventListener('click', () => {
    const name = prompt('Ordnername:');
    if (!name) return;
    fetch('/api/mkdir', {
        method: 'POST',
        headers: {'Content-Type':'application/x-www-form-urlencoded'},
        body: 'path=' + encodeURIComponent(currentPath === '/' ? '/' + name : currentPath + '/' + name)
    }).then(() => loadFiles(currentPath));
});

// ── Upload ────────────────────────────────────────────────────────────

document.getElementById('file-upload').addEventListener('change', async e => {
    const files = e.target.files;
    if (!files.length) return;
    const bar  = document.getElementById('upload-progress');
    const fill = document.getElementById('progress-fill');
    const txt  = document.getElementById('progress-text');
    bar.hidden = false;

    for (let i = 0; i < files.length; i++) {
        const file = files[i];
        const fd   = new FormData();
        fd.append('path', currentPath);
        fd.append('file', file, file.name);
        txt.textContent = `${file.name} (${i+1}/${files.length})`;
        fill.style.width = '0%';
        try {
            const xhr = new XMLHttpRequest();
            xhr.open('POST', '/api/upload');
            xhr.upload.onprogress = ev => {
                if (ev.lengthComputable) {
                    const p = Math.round(ev.loaded/ev.total*100);
                    fill.style.width = p+'%';
                    txt.textContent = `${file.name} – ${p}%`;
                }
            };
            await new Promise((r,j) => { xhr.onload=r; xhr.onerror=j; xhr.send(fd); });
        } catch(err) { console.error('Upload:', err); }
    }
    txt.textContent = 'Fertig!';
    setTimeout(() => { bar.hidden = true; }, 2000);
    loadFiles(currentPath);
    e.target.value = '';
});

// ── Rename-Dialog ─────────────────────────────────────────────────────

let _renamePath = '';
function openRenameDialog(fullPath, currentName) {
    _renamePath = fullPath;
    document.getElementById('rename-input').value = currentName;
    document.getElementById('rename-modal').hidden = false;
    document.getElementById('rename-input').focus();
    document.getElementById('rename-input').select();
}
document.getElementById('btn-rename-ok').addEventListener('click', async () => {
    const newName = document.getElementById('rename-input').value.trim();
    if (!newName) return;
    const dir  = _renamePath.substring(0, _renamePath.lastIndexOf('/'));
    const dest = (dir || '/') + '/' + newName;
    await fetch('/api/rename', {
        method: 'POST',
        headers: {'Content-Type':'application/x-www-form-urlencoded'},
        body: 'from='+encodeURIComponent(_renamePath)+'&to='+encodeURIComponent(dest)
    });
    document.getElementById('rename-modal').hidden = true;
    loadFiles(currentPath);
});
document.getElementById('btn-rename-cancel').addEventListener('click', () => {
    document.getElementById('rename-modal').hidden = true;
});

// ── Text-Editor ───────────────────────────────────────────────────────

let _editorPath = '';
async function openEditor(path, filename) {
    _editorPath = path;
    const modal  = document.getElementById('editor-modal');
    const area   = document.getElementById('editor-area');
    const status = document.getElementById('editor-status');
    document.getElementById('editor-filename').textContent = filename;
    area.value = ''; status.textContent = 'Lade …'; status.className = 'editor-status';
    modal.hidden = false;
    try {
        const res = await fetch('/api/file/read?path=' + encodeURIComponent(path));
        if (!res.ok) { status.textContent = await res.text(); status.className = 'editor-status error'; return; }
        area.value = await res.text();
        status.textContent = ''; area.focus();
    } catch(e) { status.textContent = 'Fehler: ' + e; status.className = 'editor-status error'; }
}
document.getElementById('btn-save').addEventListener('click', async () => {
    const area   = document.getElementById('editor-area');
    const status = document.getElementById('editor-status');
    status.textContent = 'Speichere …'; status.className = 'editor-status';
    try {
        const res = await fetch('/api/file/write?path=' + encodeURIComponent(_editorPath), {
            method: 'POST',
            headers: {'Content-Type':'text/plain; charset=utf-8'},
            body: area.value
        });
        status.textContent = res.ok ? 'Gespeichert.' : 'Fehler: ' + await res.text();
        status.className   = 'editor-status ' + (res.ok ? 'ok' : 'error');
    } catch(e) { status.textContent = 'Fehler: ' + e; status.className = 'editor-status error'; }
});
document.getElementById('btn-close-editor').addEventListener('click', () => {
    document.getElementById('editor-modal').hidden = true;
    loadFiles(currentPath);
});
document.getElementById('editor-area').addEventListener('keydown', e => {
    if ((e.ctrlKey || e.metaKey) && e.key === 's') {
        e.preventDefault(); document.getElementById('btn-save').click();
    }
});

// ── WiFi ──────────────────────────────────────────────────────────────

document.getElementById('btn-scan').addEventListener('click', async () => {
    const listEl = document.getElementById('network-list');
    listEl.textContent = 'Scanne …';
    await fetch('/api/wifi/scan');
    setTimeout(async () => {
        const res  = await fetch('/api/wifi/scan');
        const data = await res.json();
        listEl.innerHTML = (!data.networks?.length)
            ? 'Keine Netzwerke.'
            : data.networks.map(n =>
                `<div class="network-item" onclick="selectNet('${escHtml(n.ssid)}')">`
                + `${escHtml(n.ssid)} (${n.rssi} dBm)${n.open?' [Offen]':''}</div>`
              ).join('');
    }, 3000);
});
function selectNet(ssid) { document.getElementById('wifi-ssid-input').value = ssid; }
document.getElementById('wifi-form').addEventListener('submit', async e => {
    e.preventDefault();
    await fetch('/api/wifi/connect', {
        method: 'POST',
        headers: {'Content-Type':'application/x-www-form-urlencoded'},
        body: `ssid=${encodeURIComponent(document.getElementById('wifi-ssid-input').value)}&password=${encodeURIComponent(document.getElementById('wifi-pass-input').value)}`
    });
    alert('Verbinde …');
});

// ── Hilfsfunktionen ───────────────────────────────────────────────────

function fmtBytes(b) {
    if (!b) return '0 B';
    const u = ['B','KB','MB','GB'], i = Math.floor(Math.log(b)/Math.log(1024));
    return (b/Math.pow(1024,i)).toFixed(1)+' '+u[i];
}
function fmtUptime(s) {
    return `${Math.floor(s/3600)}h ${Math.floor((s%3600)/60)}m ${s%60}s`;
}
function fmtMs(ms) {
    const s = Math.floor(ms/1000), m = Math.floor(s/60);
    return `${String(m).padStart(2,'0')}:${String(s%60).padStart(2,'0')}.${String(ms%1000).padStart(3,'0')}`;
}
function escHtml(s) {
    return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

loadFiles('/');
