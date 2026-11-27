// Request playground: sends a request built from the form and shows the
// status line, headers and body exactly as the server returned them.

"use strict";

const presets = [
    { method: "GET", path: "/api/status" },
    { method: "GET", path: "/api/todos?done=false" },
    { method: "POST", path: "/api/todos", body: '{\n  "title": "Write more tests"\n}' },
    { method: "PATCH", path: "/api/todos/2", body: '{\n  "done": true\n}' },
    { method: "POST", path: "/api/todos", body: '{\n  "title": ""\n}', label: "POST /api/todos (empty title)" },
    { method: "POST", path: "/api/todos", body: "not json", label: "POST /api/todos (text body)" },
    { method: "GET", path: "/users/7/posts/3" },
    { method: "POST", path: "/echo", body: "Hello, server!" },
    { method: "DELETE", path: "/hello" },
    { method: "GET", path: "/missing-page" },
    { method: "GET", path: "/error" },
];

const elements = {};

function looksLikeJson(text) {
    const trimmed = text.trim();
    return trimmed.startsWith("{") || trimmed.startsWith("[");
}

function prettyBody(text, contentType) {
    if (contentType.includes("application/json")) {
        try {
            return JSON.stringify(JSON.parse(text), null, 2);
        } catch {
            // Not valid JSON after all: show it unchanged.
        }
    }
    return text;
}

function statusClass(status) {
    return `status-code s${Math.floor(status / 100)}xx`;
}

async function sendRequest() {
    const method = elements.method.value;
    const path = elements.path.value.trim() || "/";
    const body = elements.body.value;

    const init = { method, headers: {}, cache: "no-store", redirect: "manual" };
    if (body && method !== "GET" && method !== "HEAD") {
        init.body = body;
        init.headers["Content-Type"] = looksLikeJson(body) ? "application/json" : "text/plain";
    }

    elements.send.disabled = true;
    const start = performance.now();
    try {
        const response = await fetch(path, init);
        const text = await response.text();
        const elapsed = performance.now() - start;

        elements.status.className = statusClass(response.status);
        elements.status.textContent =
            response.type === "opaqueredirect" ? "3xx redirect" : `${response.status} ${response.statusText}`;
        elements.timing.textContent = `${elapsed.toFixed(1)} ms · ${text.length} B`;

        const lines = [];
        response.headers.forEach((value, name) => lines.push(`${name}: ${value}`));
        elements.headers.textContent = lines.join("\n") || "(none visible)";
        elements.output.textContent = text ? prettyBody(text, response.headers.get("content-type") || "") : "(empty)";
    } catch (error) {
        elements.status.className = "status-code s5xx";
        elements.status.textContent = "request failed";
        elements.timing.textContent = "";
        elements.headers.textContent = "";
        elements.output.textContent = error.message;
    } finally {
        elements.response.hidden = false;
        elements.send.disabled = false;
    }
}

function applyPreset(preset) {
    elements.method.value = preset.method;
    elements.path.value = preset.path;
    elements.body.value = preset.body || "";
    sendRequest();
}

document.addEventListener("DOMContentLoaded", () => {
    elements.form = document.getElementById("request-form");
    elements.method = document.getElementById("method");
    elements.path = document.getElementById("path");
    elements.body = document.getElementById("body");
    elements.send = document.getElementById("send");
    elements.response = document.getElementById("response");
    elements.status = document.getElementById("response-status");
    elements.timing = document.getElementById("response-timing");
    elements.headers = document.getElementById("response-headers");
    elements.output = document.getElementById("response-body");

    const presetBox = document.getElementById("presets");
    for (const preset of presets) {
        const button = document.createElement("button");
        button.type = "button";
        button.textContent = preset.label || `${preset.method} ${preset.path}`;
        button.addEventListener("click", () => applyPreset(preset));
        presetBox.append(button);
    }

    elements.form.addEventListener("submit", (event) => {
        event.preventDefault();
        sendRequest();
    });
});
