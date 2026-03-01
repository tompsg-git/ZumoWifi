// ZumoWifi – Web Interface

let currentPath = "/";

// ── Tab Navigation ──────────────────────────────────────────────────

document.querySelectorAll(".tab").forEach(btn => {
    btn.addEventListener("click", () => {
        document.querySelectorAll(".tab").forEach(t => t.classList.remove("active"));
        document.querySelectorAll(".tab-content").forEach(c => c.classList.remove("active"));
        btn.classList.add("active");
        document.getElementById("tab-" + btn.dataset.tab).classList.add("active");
    });
});

// ── Status Polling ──────────────────────────────────────────────────

async function fetchStatus() {
    try {
        const res = await fetch("/api/status");
        const data = await res.json();

        const badge = document.getElementById("status-badge");
        if (data.device !== "disconnected") {
            badge.textContent = data.device;
            badge.className = "badge online";
        } else {
            badge.textContent = "Disconnected";
            badge.className = "badge offline";
        }

        document.getElementById("wifi-mode").textContent = data.wifi_mode;
        document.getElementById("wifi-ssid").textContent = data.wifi_ssid;
        document.getElementById("wifi-ip").textContent = data.wifi_ip;
        document.getElementById("info-device").textContent = data.device;
        document.getElementById("info-heap").textContent = formatBytes(data.heap_free);
        document.getElementById("info-uptime").textContent = formatUptime(data.uptime);
    } catch (e) {
        console.error("Status fetch failed:", e);
    }
}

setInterval(fetchStatus, 5000);
fetchStatus();

// ── File Browser ────────────────────────────────────────────────────

async function loadFiles(path) {
    currentPath = path || "/";
    document.getElementById("current-path").textContent = currentPath;

    const listEl = document.getElementById("file-list");

    try {
        const res = await fetch("/api/files?path=" + encodeURIComponent(currentPath));
        if (!res.ok) {
            listEl.innerHTML = '<p class="placeholder">Device not connected.</p>';
            return;
        }

        const data = await res.json();
        if (!data.files || data.files.length === 0) {
            listEl.innerHTML = '<p class="placeholder">Empty directory.</p>';
            return;
        }

        listEl.innerHTML = data.files.map(f => `
            <div class="file-entry" data-name="${f.name}" data-dir="${f.isDir}">
                <div class="name">
                    <span class="icon">${f.isDir ? "\uD83D\uDCC1" : "\uD83D\uDCC4"}</span>
                    <span>${f.name}</span>
                </div>
                <span class="size">${f.isDir ? "" : formatBytes(f.size)}</span>
                <div class="actions">
                    ${f.isDir ? "" : '<button class="dl-btn" onclick="downloadFile(event)">DL</button>'}
                    <button onclick="deleteEntry(event)">Del</button>
                </div>
            </div>
        `).join("");

        // Click on directory to navigate
        listEl.querySelectorAll('.file-entry[data-dir="true"]').forEach(el => {
            el.addEventListener("click", (e) => {
                if (e.target.tagName === "BUTTON") return;
                const name = el.dataset.name;
                loadFiles(currentPath === "/" ? "/" + name : currentPath + "/" + name);
            });
        });
    } catch (e) {
        listEl.innerHTML = '<p class="placeholder">Error loading files.</p>';
    }
}

document.getElementById("btn-up").addEventListener("click", () => {
    if (currentPath === "/") return;
    const parts = currentPath.split("/").filter(Boolean);
    parts.pop();
    loadFiles("/" + parts.join("/"));
});

document.getElementById("btn-refresh").addEventListener("click", () => loadFiles(currentPath));

document.getElementById("btn-mkdir").addEventListener("click", () => {
    const name = prompt("Folder name:");
    if (!name) return;
    const path = currentPath === "/" ? "/" + name : currentPath + "/" + name;
    fetch("/api/mkdir", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "path=" + encodeURIComponent(path)
    }).then(() => loadFiles(currentPath));
});

// ── File Upload ─────────────────────────────────────────────────────

document.getElementById("file-upload").addEventListener("change", async (e) => {
    const files = e.target.files;
    if (!files.length) return;

    const progressBar = document.getElementById("upload-progress");
    const progressFill = document.getElementById("progress-fill");
    const progressText = document.getElementById("progress-text");
    progressBar.hidden = false;

    for (let i = 0; i < files.length; i++) {
        const file = files[i];
        const formData = new FormData();
        formData.append("path", currentPath);
        formData.append("file", file, file.name);

        progressText.textContent = `Uploading ${file.name}... (${i + 1}/${files.length})`;
        progressFill.style.width = "0%";

        try {
            const xhr = new XMLHttpRequest();
            xhr.open("POST", "/api/upload");

            xhr.upload.onprogress = (ev) => {
                if (ev.lengthComputable) {
                    const pct = Math.round((ev.loaded / ev.total) * 100);
                    progressFill.style.width = pct + "%";
                    progressText.textContent = `${file.name} – ${pct}%`;
                }
            };

            await new Promise((resolve, reject) => {
                xhr.onload = resolve;
                xhr.onerror = reject;
                xhr.send(formData);
            });
        } catch (err) {
            console.error("Upload failed:", err);
        }
    }

    progressText.textContent = "Done!";
    setTimeout(() => { progressBar.hidden = true; }, 2000);
    loadFiles(currentPath);
    e.target.value = "";
});

// ── Download & Delete ───────────────────────────────────────────────

function downloadFile(e) {
    e.stopPropagation();
    const entry = e.target.closest(".file-entry");
    const name = entry.dataset.name;
    const path = currentPath === "/" ? "/" + name : currentPath + "/" + name;
    window.location.href = "/api/download?path=" + encodeURIComponent(path);
}

function deleteEntry(e) {
    e.stopPropagation();
    const entry = e.target.closest(".file-entry");
    const name = entry.dataset.name;
    if (!confirm("Delete " + name + "?")) return;
    const path = currentPath === "/" ? "/" + name : currentPath + "/" + name;
    fetch("/api/delete", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "path=" + encodeURIComponent(path)
    }).then(() => loadFiles(currentPath));
}

// ── WiFi ────────────────────────────────────────────────────────────

document.getElementById("btn-scan").addEventListener("click", async () => {
    const listEl = document.getElementById("network-list");
    listEl.innerHTML = "Scanning...";

    // First call triggers async scan
    await fetch("/api/wifi/scan");

    // Wait and fetch results
    setTimeout(async () => {
        const res = await fetch("/api/wifi/scan");
        const data = await res.json();

        if (!data.networks || data.networks.length === 0) {
            listEl.innerHTML = "No networks found.";
            return;
        }

        listEl.innerHTML = data.networks.map(n => `
            <div class="network-item" onclick="selectNetwork('${n.ssid}')">
                ${n.ssid} (${n.rssi} dBm) ${n.open ? "[Open]" : ""}
            </div>
        `).join("");
    }, 3000);
});

function selectNetwork(ssid) {
    document.getElementById("wifi-ssid-input").value = ssid;
}

document.getElementById("wifi-form").addEventListener("submit", async (e) => {
    e.preventDefault();
    const ssid = document.getElementById("wifi-ssid-input").value;
    const pass = document.getElementById("wifi-pass-input").value;

    await fetch("/api/wifi/connect", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(pass)}`
    });

    alert("Connecting to " + ssid + "...\nThe page may reload with a new IP.");
});

// ── Helpers ─────────────────────────────────────────────────────────

function formatBytes(bytes) {
    if (bytes === 0) return "0 B";
    const k = 1024;
    const sizes = ["B", "KB", "MB", "GB"];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + " " + sizes[i];
}

function formatUptime(seconds) {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return `${h}h ${m}m ${s}s`;
}

// Initial file load
loadFiles("/");
