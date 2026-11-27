// Shared helpers for the demo website: JSON fetch wrapper and the
// server status shown in every page footer.

"use strict";

// Calls the JSON API and returns the parsed body. Error responses from the
// server look like {"error":{"status":404,"message":"..."}} and are thrown
// as Error objects carrying that message.
async function apiFetch(path, options = {}) {
    const init = { ...options, headers: { Accept: "application/json", ...options.headers } };
    if (init.body !== undefined && typeof init.body !== "string") {
        init.body = JSON.stringify(init.body);
        init.headers["Content-Type"] = "application/json";
    }

    const response = await fetch(path, init);
    if (response.status === 204) {
        return null;
    }

    const text = await response.text();
    let data = null;
    try {
        data = text ? JSON.parse(text) : null;
    } catch {
        throw new Error(`unexpected response from ${path} (${response.status})`);
    }
    if (!response.ok) {
        const message = data && data.error ? data.error.message : response.statusText;
        throw new Error(message);
    }
    return data;
}

function formatUptime(seconds) {
    const total = Math.floor(seconds);
    const days = Math.floor(total / 86400);
    const hours = Math.floor((total % 86400) / 3600);
    const minutes = Math.floor((total % 3600) / 60);
    const secs = total % 60;
    if (days > 0) return `${days}d ${hours}h`;
    if (hours > 0) return `${hours}h ${minutes}m`;
    if (minutes > 0) return `${minutes}m ${secs}s`;
    return `${secs}s`;
}

async function updateFooterStatus() {
    const pill = document.getElementById("server-status");
    if (!pill) return;
    try {
        const status = await apiFetch("/api/status");
        pill.className = "pill ok";
        pill.textContent = `${status.name} ${status.version} · up ${formatUptime(status.uptime_seconds)}`;
    } catch {
        pill.className = "pill error";
        pill.textContent = "server unreachable";
    }
}

document.addEventListener("DOMContentLoaded", updateFooterStatus);
