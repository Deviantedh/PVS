const fs = require("fs");
const vm = require("vm");

class Element {
  constructor(tagName = "div") {
    this.tagName = tagName;
    this.textContent = "";
    this.value = "";
    this.disabled = false;
    this.children = [];
    this.listeners = {};
    this.className = "";
    this.title = "";
  }

  replaceChildren(...children) {
    this.children = children;
  }

  append(...children) {
    this.children.push(...children);
  }

  addEventListener(name, callback) {
    this.listeners[name] = callback;
  }
}

const elements = new Map();

function createElement(id, value = "") {
  const element = new Element(id);
  element.value = value;
  elements.set(id, element);
  return element;
}

const form = createElement("run-form");
form.elements = {
  backend_url: createElement("backend-url", "http://127.0.0.1:8080/api/run"),
  job_id: createElement("job-id", "test"),
  firmware: createElement("firmware", "AAEAIAkAAAgAv/7n"),
  max_instructions: createElement("max-instructions", "1"),
  timeout_ms: createElement("timeout-ms", "1000"),
  uart_input: createElement("uart-input", "")
};

for (const id of [
  "pins-left",
  "pins-right",
  "run-button",
  "create-session-button",
  "step-session-button",
  "run-session-button",
  "stop-session-button",
  "request-state",
  "run-status",
  "stop-reason",
  "uart-output",
  "session-summary",
  "result-summary",
  "cpu-snapshot",
  "peripheral-snapshot",
  "raw-result"
]) {
  if (!elements.has(id)) {
    createElement(id);
  }
}

const document = {
  getElementById(id) {
    const element = elements.get(id);
    if (!element) {
      throw new Error(`missing element: ${id}`);
    }
    return element;
  },
  createElement(tagName) {
    return new Element(tagName);
  }
};

const context = { document, fetch: async () => { throw new Error("not used"); }, console };
vm.createContext(context);
vm.runInContext(fs.readFileSync("gui/app.js", "utf8"), context);

context.renderResult({ status: "OK", pins: [] });
if (elements.get("pins-left").children.length !== 1 || elements.get("pins-left").children[0].children[1].textContent !== "no data") {
  throw new Error("empty pins array should render a no-data state");
}

context.renderResult({
  status: "OK",
  pins: [
    { name: "PA2", port: "A", index: 2, mode: "unknown", level: null, label: "USART1_TX" },
    { name: "PC13", port: "C", index: 13, mode: "unknown", level: null, label: "LED" }
  ]
});

const firstPin = elements.get("pins-left").children[0];
if (firstPin.children[0].textContent !== "PA2") {
  throw new Error("non-empty pins array should render pin names");
}
if (!firstPin.children[1].textContent.includes("USART1_TX")) {
  throw new Error("pin label should be visible");
}
