// Home page: live server numbers and the endpoint list from GET /api.

"use strict";

function setText(id, text) {
    const element = document.getElementById(id);
    if (element) element.textContent = text;
}

async function loadStats() {
    try {
        const status = await apiFetch("/api/status");
        setText("stat-uptime", formatUptime(status.uptime_seconds));
        setText("stat-todos", String(status.todos));
        setText("stat-version", status.version);
    } catch {
        setText("stat-uptime", "offline");
    }

    try {
        const start = performance.now();
        await apiFetch("/health", { cache: "no-store" });
        setText("stat-latency", `${(performance.now() - start).toFixed(1)} ms`);
    } catch {
        setText("stat-latency", "–");
    }
}

async function loadEndpoints() {
    const body = document.getElementById("endpoints");
    try {
        const index = await apiFetch("/api");
        body.replaceChildren(
            ...index.endpoints.map((endpoint) => {
                const row = document.createElement("tr");

                const route = document.createElement("td");
                const method = document.createElement("span");
                method.className = "method";
                method.textContent = endpoint.method;
                const path = document.createElement("code");
                path.textContent = endpoint.path;
                route.append(method, path);

                const description = document.createElement("td");
                description.textContent = endpoint.description;

                row.append(route, description);
                return row;
            })
        );
    } catch (error) {
        const row = document.createElement("tr");
        const cell = document.createElement("td");
        cell.colSpan = 2;
        cell.textContent = `Could not load /api: ${error.message}`;
        row.append(cell);
        body.replaceChildren(row);
    }
}

document.addEventListener("DOMContentLoaded", () => {
    loadStats();
    loadEndpoints();
});
