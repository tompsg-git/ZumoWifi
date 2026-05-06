// ZumoWifi – Web-Interface

'use strict';

let currentPath = '/';

// Erweiterungen, die im Editor geöffnet werden können
const TEXT_EXTS = new Set([
  'txt','xml','gpx','json','csv','nmea','log','ini','cfg','kml',
  'htm','html','js','css','md','yaml','yml','nmea','ov2','wpr'
]);

function isTextFile(name) {
    const ext = name.split('.').pop().toLowerCase();
    return TEXT_EXTS.has(ext);
}

// ── Tab-Navigation ───────────────────────────────────────────────────

document.querySelectorAll('.tab').forEach(btn => {
    btn.addEventListener('click', () => {
        document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
        document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
        btn.classList.add('active');
        document.getElementById('tab-' + btn.dataset.tab).classList.add('active');
    });
});

// ── Status-Polling ───────────────────────────────────────────────────

async function fetchStatus() {
    try {
        const res  = await fetch('/api/status');
        const data = await res.json();

        const badge = document.getElementById('status-badge');
        if (data.device !== 'disconnected') {
            badge.textContent = data.device;
            badge.className   = 'badge online';
        } else {
            badge.textContent = 'Kein Stick';
            badge.className   = 'badge offline';
        }

        document.getElementById('wifi-mode').textContent  = data.wifi_mode;
        document.getElementById('wifi-ssid').textContent  = data.wifi_ssid;
        document.getElementById('wifi-ip').textContent    = data.wifi_ip;
        document.getElementById('info-device').textContent = data.device;
        document.getElementById('info-heap').textContent  = fmtBytes(data.heap_free);
        document.getElementById('info-uptime').textContent = fmtUptime(data.uptime);
    } catch (_) {}
}

setInterval(fetchStatus, 5000);
fetchStatus();

// ── Datei-Browser ────────────────────────────────────────────────────

async function loadFiles(path) {
    currentPath = path || '/';
    document.getElementById('current-path').textContent = currentPath;
    const listEl = document.getElementById('file-list');
    listEl.innerHTML = '<p class="placeholder">Lade …</p>';

    try {
        const res = await fetch('/api/files?path=' + encodeURIComponent(currentPath));
        if (!res.ok) {
            listEl.innerHTML = '<p class="placeholder">Stick nicht verbunden.</p>';
            return;
        }
        const data = await res.json();

        if (!data.files || data.files.length === 0) {
            listEl.innerHTML = '<p class="placeholder">Leeres Verzeichnis.</p>';
            return;
        }

        // Sortierung: Ordner zuerst, dann alphabetisch
        data.files.sort((a, b) => {
            if (a.isDir !== b.isDir) return a.isDir ? -1 : 1;
            return a.name.localeCompare(b.name);
        });

        listEl.innerHTML = '';
        for (const f of data.files) {
            listEl.appendChild(buildEntry(f));
        }
    } catch (_) {
        listEl.innerHTML = '<p class="placeholder">Fehler beim Laden.</p>';
    }
}

function buildEntry(f) {
    const div = document.createElement('div');
    div.className = 'file-entry';
    div.dataset.name  = f.name;
    div.dataset.isDir = f.isDir;

    const icon = f.isDir ? '&#128193;' : (isTextFile(f.name) ? '&#128203;' : '&#128196;');

    div.innerHTML = `
        <div class="name">
            <span class="icon">${icon}</span>
            <span>${escHtml(f.name)}</span>
        </div>
        <span class="size">${f.isDir ? '' : fmtBytes(f.size)}</span>
        <div class="actions">
            ${!f.isDir && isTextFile(f.name)
              ? '<button class="edit-btn" title="Bearbeiten">&#9998;</button>'
              : ''}
            ${!f.isDir
              ? '<button class="dl-btn" title="Herunterladen">&#8595;</button>'
              : ''}
            <button class="ren-btn" title="Umbenennen">&#9998;&#8197;&#9998;</button>
            <button class="del-btn" title="Löschen">&#128465;</button>
        </div>`;

    const filePath = currentPath === '/'
        ? '/' + f.name
        : currentPath + '/' + f.name;

    // Ordner: Klick navigiert rein
    if (f.isDir) {
        div.addEventListener('click', e => {
            if (e.target.closest('button')) return;
            loadFiles(filePath);
        });
    }

    // Edit-Button (nur Textdateien)
    const editBtn = div.querySelector('.edit-btn');
    if (editBtn) {
        editBtn.addEventListener('click', e => {
            e.stopPropagation();
            openEditor(filePath, f.name);
        });
    }

    // Download-Button
    const dlBtn = div.querySelector('.dl-btn');
    if (dlBtn) {
        dlBtn.addEventListener('click', e => {
            e.stopPropagation();
            window.location.href = '/api/download?path=' + encodeURIComponent(filePath);
        });
    }

    // Rename-Button
    div.querySelector('.ren-btn').addEventListener('click', e => {
        e.stopPropagation();
        openRenameDialog(filePath, f.name);
    });

    // Delete-Button
    div.querySelector('.del-btn').addEventListener('click', e => {
        e.stopPropagation();
        if (!confirm('Löschen: ' + f.name + '?')) return;
        fetch('/api/delete', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: 'path=' + encodeURIComponent(filePath)
        }).then(() => loadFiles(currentPath));
    });

    return div;
}

// ── Navigation ────────────────────────────────────────────────────────

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
    const path = currentPath === '/' ? '/' + name : currentPath + '/' + name;
    fetch('/api/mkdir', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'path=' + encodeURIComponent(path)
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

        txt.textContent = `${file.name} (${i + 1}/${files.length})`;
        fill.style.width = '0%';

        try {
            const xhr = new XMLHttpRequest();
            xhr.open('POST', '/api/upload');
            xhr.upload.onprogress = ev => {
                if (ev.lengthComputable) {
                    const p = Math.round(ev.loaded / ev.total * 100);
                    fill.style.width = p + '%';
                    txt.textContent  = `${file.name} – ${p}%`;
                }
            };
            await new Promise((res, rej) => {
                xhr.onload  = res;
                xhr.onerror = rej;
                xhr.send(fd);
            });
        } catch (err) {
            console.error('Upload-Fehler:', err);
        }
    }

    txt.textContent = 'Fertig!';
    setTimeout(() => { bar.hidden = true; }, 2000);
    loadFiles(currentPath);
    e.target.value = '';
});

// ── Rename-Dialog ────────────────────────────────────────────────────

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
    const dest = (dir === '' ? '/' : dir) + '/' + newName;

    await fetch('/api/rename', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'from=' + encodeURIComponent(_renamePath) +
              '&to='  + encodeURIComponent(dest)
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
    area.value        = '';
    status.textContent = 'Lade …';
    status.className   = 'editor-status';
    modal.hidden       = false;

    try {
        const res = await fetch('/api/file/read?path=' + encodeURIComponent(path));
        if (!res.ok) {
            status.textContent = 'Fehler: ' + (await res.text());
            status.className   = 'editor-status error';
            return;
        }
        area.value        = await res.text();
        status.textContent = '';
        area.focus();
    } catch (err) {
        status.textContent = 'Netzwerkfehler: ' + err;
        status.className   = 'editor-status error';
    }
}

document.getElementById('btn-save').addEventListener('click', async () => {
    const area   = document.getElementById('editor-area');
    const status = document.getElementById('editor-status');

    status.textContent = 'Speichere …';
    status.className   = 'editor-status';

    try {
        const res = await fetch('/api/file/write?path=' + encodeURIComponent(_editorPath), {
            method: 'POST',
            headers: { 'Content-Type': 'text/plain; charset=utf-8' },
            body: area.value
        });
        if (res.ok) {
            status.textContent = 'Gespeichert.';
            status.className   = 'editor-status ok';
        } else {
            status.textContent = 'Fehler: ' + (await res.text());
            status.className   = 'editor-status error';
        }
    } catch (err) {
        status.textContent = 'Netzwerkfehler: ' + err;
        status.className   = 'editor-status error';
    }
});

document.getElementById('btn-close-editor').addEventListener('click', () => {
    document.getElementById('editor-modal').hidden = true;
    // Dateiliste nach möglichem Speichern neu laden
    loadFiles(currentPath);
});

// Ctrl+S im Editor-Textarea speichert
document.getElementById('editor-area').addEventListener('keydown', e => {
    if ((e.ctrlKey || e.metaKey) && e.key === 's') {
        e.preventDefault();
        document.getElementById('btn-save').click();
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
        if (!data.networks || !data.networks.length) {
            listEl.textContent = 'Keine Netzwerke gefunden.';
            return;
        }
        listEl.innerHTML = data.networks.map(n =>
            `<div class="network-item" onclick="selectNet('${escHtml(n.ssid)}')">`
            + `${escHtml(n.ssid)} (${n.rssi} dBm)${n.open ? ' [Offen]' : ''}</div>`
        ).join('');
    }, 3000);
});

function selectNet(ssid) {
    document.getElementById('wifi-ssid-input').value = ssid;
}

document.getElementById('wifi-form').addEventListener('submit', async e => {
    e.preventDefault();
    const ssid = document.getElementById('wifi-ssid-input').value;
    const pass = document.getElementById('wifi-pass-input').value;
    await fetch('/api/wifi/connect', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(pass)}`
    });
    alert('Verbinde mit ' + ssid + ' …\nDie Seite wird möglicherweise mit neuer IP geladen.');
});

// ── Hilfsfunktionen ───────────────────────────────────────────────────

function fmtBytes(b) {
    if (!b) return '0 B';
    const k = 1024, units = ['B','KB','MB','GB'];
    const i = Math.floor(Math.log(b) / Math.log(k));
    return (b / Math.pow(k, i)).toFixed(1) + ' ' + units[i];
}

function fmtUptime(s) {
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    const sec = s % 60;
    return `${h}h ${m}m ${sec}s`;
}

function escHtml(str) {
    return str.replace(/&/g,'&amp;').replace(/</g,'&lt;')
              .replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

// Erste Dateiliste laden
loadFiles('/');
