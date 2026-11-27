// To-do app: a client for the /api/todos endpoints.

"use strict";

const state = {
    filter: "all", // "all", "true" (done) or "false" (open)
};

const elements = {};

function showMessage(text, isError = false) {
    elements.message.textContent = text;
    elements.message.className = isError ? "message error" : "message";
}

function todoItem(todo) {
    const item = document.createElement("li");
    item.className = todo.done ? "done" : "";

    const checkbox = document.createElement("input");
    checkbox.type = "checkbox";
    checkbox.checked = todo.done;
    checkbox.setAttribute("aria-label", `Mark "${todo.title}" as ${todo.done ? "open" : "done"}`);
    checkbox.addEventListener("change", () => updateTodo(todo.id, { done: checkbox.checked }));

    const title = document.createElement("span");
    title.className = "title";
    title.textContent = todo.title;

    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "link";
    remove.textContent = "Delete";
    remove.setAttribute("aria-label", `Delete "${todo.title}"`);
    remove.addEventListener("click", () => deleteTodo(todo.id));

    item.append(checkbox, title, remove);
    return item;
}

function render(todos) {
    if (todos.length === 0) {
        const empty = document.createElement("li");
        empty.className = "empty";
        empty.textContent = state.filter === "all" ? "Nothing to do. Add something above." : "No matching todos.";
        elements.list.replaceChildren(empty);
    } else {
        elements.list.replaceChildren(...todos.map(todoItem));
    }
}

async function loadTodos() {
    const query = state.filter === "all" ? "" : `?done=${state.filter}`;
    try {
        const [filtered, all] = await Promise.all([
            apiFetch(`/api/todos${query}`),
            query ? apiFetch("/api/todos") : null,
        ]);
        render(filtered.todos);
        const everything = all ? all.todos : filtered.todos;
        const open = everything.filter((todo) => !todo.done).length;
        elements.meta.textContent = `${everything.length} total · ${open} open · ${everything.length - open} done`;
    } catch (error) {
        showMessage(`Could not load todos: ${error.message}`, true);
    }
}

async function addTodo(title) {
    const todo = await apiFetch("/api/todos", { method: "POST", body: { title } });
    showMessage(`Created todo #${todo.id}.`);
}

async function updateTodo(id, changes) {
    try {
        await apiFetch(`/api/todos/${id}`, { method: "PATCH", body: changes });
        showMessage("");
    } catch (error) {
        showMessage(`Could not update todo #${id}: ${error.message}`, true);
    }
    await loadTodos();
}

async function deleteTodo(id) {
    try {
        await apiFetch(`/api/todos/${id}`, { method: "DELETE" });
        showMessage(`Deleted todo #${id}.`);
    } catch (error) {
        showMessage(`Could not delete todo #${id}: ${error.message}`, true);
    }
    await loadTodos();
}

function setFilter(filter) {
    state.filter = filter;
    for (const button of document.querySelectorAll("[data-filter]")) {
        button.setAttribute("aria-pressed", String(button.dataset.filter === filter));
    }
    loadTodos();
}

document.addEventListener("DOMContentLoaded", () => {
    elements.form = document.getElementById("todo-form");
    elements.input = document.getElementById("todo-title");
    elements.list = document.getElementById("todo-list");
    elements.meta = document.getElementById("todo-meta");
    elements.message = document.getElementById("todo-message");

    elements.form.addEventListener("submit", async (event) => {
        event.preventDefault();
        const submit = elements.form.querySelector("button[type=submit]");
        submit.disabled = true;
        try {
            // Validation (empty or too long titles) happens on the server.
            await addTodo(elements.input.value);
            elements.input.value = "";
            await loadTodos();
        } catch (error) {
            showMessage(error.message, true);
        } finally {
            submit.disabled = false;
            elements.input.focus();
        }
    });

    for (const button of document.querySelectorAll("[data-filter]")) {
        button.addEventListener("click", () => setFilter(button.dataset.filter));
    }
    document.getElementById("refresh").addEventListener("click", loadTodos);

    loadTodos();
});
