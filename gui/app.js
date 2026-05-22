let activeSessionId = "";
let eventSource = null;
let latestPins = [];

function renderKeyValues(nodeId, values) {
  const node = document.getElementById(nodeId);
  node.replaceChildren();

  for (const [key, value] of Object.entries(values)) {
    const label = document.createElement("dt");
    const data = document.createElement("dd");
    label.textContent = key;
    data.textContent = value;
    node.append(label, data);
  }
}

function renderPins(pins = []) {
  const left = document.getElementById("pins-left");
  const right = document.getElementById("pins-right");
  left.replaceChildren();
  right.replaceChildren();

  if (!Array.isArray(pins) || pins.length === 0) {
    const pin = document.createElement("div");
    pin.className = "pin";
    pin.replaceChildren(textNode("strong", "pins"), textNode("span", "no data"));
    left.append(pin);
    return;
  }

  pins.forEach((snapshot, index) => {
    const signal = formatPinSignal(snapshot);
    const pin = document.createElement("div");
    pin.className = snapshot.mode === "unknown" ? "pin" : "pin active";
    pin.title = `port=${snapshot.port ?? ""} index=${snapshot.index ?? ""} mode=${snapshot.mode ?? ""}`;
    pin.addEventListener("click", () => controlPin(snapshot));
    pin.replaceChildren(textNode("strong", snapshot.name || `${snapshot.port}${snapshot.index}`), textNode("span", signal));
    (index < pins.length / 2 ? left : right).append(pin);
  });
}

function formatPinSignal(pin) {
  const level = pin.level === null || pin.level === undefined ? "?" : String(pin.level);
  const label = pin.label ? `${pin.label} ` : "";
  return `${label}${pin.mode || "unknown"}:${level}`;
}

function textNode(tagName, value) {
  const node = document.createElement(tagName);
  node.textContent = value;
  return node;
}

function buildJob(form) {
  const fields = form.elements;
  const maxInstructions = Number(fields.max_instructions.value);
  const timeoutMs = Number(fields.timeout_ms.value);

  return {
    job_id: fields.job_id.value.trim(),
    firmware: fields.firmware.value.replace(/\s+/g, ""),
    config: {
      max_instructions: Number.isFinite(maxInstructions) && maxInstructions > 0 ? maxInstructions : 0,
      timeout_ms: Number.isFinite(timeoutMs) && timeoutMs > 0 ? timeoutMs : 0,
      uart_input: fields.uart_input.value
    }
  };
}

function sessionURL(path = "") {
  const endpoint = document.getElementById("run-form").elements.backend_url.value.trim();
  const base = endpoint.replace(/\/api\/run$/, "/api/session");
  return `${base}${path}`;
}

function setSessionControls(enabled) {
  document.getElementById("step-session-button").disabled = !enabled;
  document.getElementById("run-session-button").disabled = !enabled;
  document.getElementById("stop-session-button").disabled = !enabled;
}

async function runSimulation(event) {
  event.preventDefault();

  const form = event.currentTarget;
  const button = document.getElementById("run-button");
  const requestState = document.getElementById("request-state");
  const endpoint = form.elements.backend_url.value.trim();
  const job = buildJob(form);

  setRunning(true);
  requestState.textContent = "sending request";
  renderPending(job.job_id);

  try {
    const response = await fetch(endpoint, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(job)
    });
    const text = await response.text();
    const result = parseResult(text);

    if (!response.ok && !result.error) {
      result.error = `HTTP ${response.status}`;
    }

    requestState.textContent = response.ok ? "completed" : `completed with HTTP ${response.status}`;
    renderResult(result);
  } catch (error) {
    requestState.textContent = "request failed";
    renderResult({
      job_id: job.job_id,
      status: "API_ERROR",
      exit_code: 0,
      error_code: "HTTP_REQUEST_FAILED",
      error: error.message
    });
  } finally {
    button.disabled = false;
  }
}

async function createSession() {
  const form = document.getElementById("run-form");
  const requestState = document.getElementById("request-state");
  const job = buildJob(form);

  requestState.textContent = "creating session";
  try {
    const state = await postJSON(sessionURL(), job);
    activeSessionId = state.session_id || "";
    setSessionControls(activeSessionId !== "");
    startEventStream(activeSessionId);
    requestState.textContent = activeSessionId ? "session created" : "session create returned no id";
    renderSessionState(state);
  } catch (error) {
    requestState.textContent = "session create failed";
    renderResult({
      status: "API_ERROR",
      error_code: "SESSION_CREATE_FAILED",
      error: error.message,
      pins: latestPins
    });
  }
}

async function stepSession() {
  await sessionAction("step", { steps: 1 });
}

async function runSession() {
  await sessionAction("run", { max_instructions: Number(document.getElementById("max-instructions").value) || 1000 });
}

async function stopSession() {
  await sessionAction("stop", {});
}

async function sessionAction(action, body) {
  if (!activeSessionId) {
    return;
  }

  const requestState = document.getElementById("request-state");
  requestState.textContent = `${action} session`;
  try {
    const state = await postJSON(sessionURL(`/${activeSessionId}/${action}`), body);
    requestState.textContent = `${action} completed`;
    renderSessionState(state);
  } catch (error) {
    requestState.textContent = `${action} failed`;
    renderResult({
      status: "API_ERROR",
      error_code: "SESSION_ACTION_FAILED",
      error: error.message,
      pins: latestPins
    });
  }
}

async function controlPin(pin) {
  if (!activeSessionId || !pin?.name) {
    return;
  }

  const nextLevel = pin.level === 1 ? 0 : 1;
  try {
    const state = await postJSON(sessionURL(`/${activeSessionId}/pins/${encodeURIComponent(pin.name)}`), {
      level: nextLevel,
      mode: "input"
    });
    renderSessionState(state);
  } catch (error) {
    document.getElementById("request-state").textContent = `pin update failed: ${error.message}`;
  }
}

async function postJSON(url, body) {
  const response = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  });
  const text = await response.text();
  const result = parseResult(text);
  if (!response.ok) {
    throw new Error(result.error || result.error_code || `HTTP ${response.status}`);
  }
  return result;
}

function startEventStream(sessionId) {
  if (!sessionId || typeof EventSource === "undefined") {
    return;
  }
  if (eventSource) {
    eventSource.close();
  }

  eventSource = new EventSource(sessionURL(`/${sessionId}/events`));
  eventSource.addEventListener("snapshot", (event) => {
    renderSessionState(JSON.parse(event.data));
  });
  eventSource.onerror = () => {
    document.getElementById("request-state").textContent = "event stream disconnected";
  };
}

function parseResult(text) {
  if (text.trim() === "") {
    return { status: "EMPTY_RESPONSE", error_code: "EMPTY_RESPONSE", error: "backend returned empty response" };
  }

  try {
    return JSON.parse(text);
  } catch (error) {
    return {
      status: "INVALID_JSON",
      error_code: "INVALID_BACKEND_JSON",
      error: error.message,
      stdout: text
    };
  }
}

function setRunning(isRunning) {
  document.getElementById("run-button").disabled = isRunning;
  document.getElementById("run-status").textContent = isRunning ? "running" : "idle";
}

function renderPending(jobId) {
  document.getElementById("run-status").textContent = "running";
  document.getElementById("stop-reason").textContent = `job ${jobId || "(empty)"} submitted`;
  document.getElementById("uart-output").textContent = "(waiting)";
  renderKeyValues("result-summary", {
    job_id: jobId || "(empty)",
    status: "running"
  });
  renderKeyValues("cpu-snapshot", {});
  renderKeyValues("peripheral-snapshot", {});
  document.getElementById("raw-result").textContent = "";
}

function renderResult(result) {
  document.getElementById("run-status").textContent = result.status || "(unknown)";
  document.getElementById("stop-reason").textContent = result.error_code || result.error || "result received";
  document.getElementById("uart-output").textContent = result.uart_output || "(empty)";

  renderKeyValues("result-summary", {
    job_id: result.job_id || "",
    status: result.status || "",
    error_code: result.error_code || "",
    error: result.error || "",
    exit_code: String(result.exit_code ?? ""),
    instructions_executed: String(result.instructions_executed ?? "")
  });
  renderKeyValues("cpu-snapshot", snapshotValues(result.cpu, ["pc", "msp", "lr", "xpsr", "primask", "instr_count"]));
  renderKeyValues("peripheral-snapshot", {
    tim2: formatObject(result.peripherals?.tim2),
    usart1: formatObject(result.peripherals?.usart1),
    nvic: formatObject(result.peripherals?.nvic),
    pins: Array.isArray(result.pins) ? `${result.pins.length} item(s)` : ""
  });
  latestPins = result.pins || [];
  renderPins(latestPins);
  document.getElementById("raw-result").textContent = JSON.stringify(result, null, 2);
}

function renderSessionState(state) {
  activeSessionId = state.session_id || activeSessionId;
  setSessionControls(activeSessionId !== "");
  document.getElementById("run-status").textContent = state.status || "(unknown)";
  document.getElementById("stop-reason").textContent = state.stop_reason || state.error_code || state.error || "session state";
  document.getElementById("uart-output").textContent = state.uart_output || "(empty)";
  renderKeyValues("session-summary", {
    session_id: activeSessionId,
    status: state.status || "",
    stop_reason: state.stop_reason || "",
    instructions_executed: String(state.instructions_executed ?? ""),
    error_code: state.error_code || ""
  });
  renderResult({
    job_id: activeSessionId,
    status: state.status,
    error_code: state.error_code,
    error: state.error,
    exit_code: "",
    instructions_executed: state.instructions_executed,
    uart_output: state.uart_output,
    cpu: state.cpu,
    peripherals: state.peripherals,
    pins: state.pins || []
  });
}

function snapshotValues(value, keys) {
  if (!value) {
    return {};
  }

  return Object.fromEntries(keys.map((key) => [key, formatValue(value[key])]));
}

function formatObject(value) {
  if (!value) {
    return "";
  }
  return Object.entries(value)
    .map(([key, item]) => `${key}=${formatValue(item)}`)
    .join(" ");
}

function formatValue(value) {
  if (Array.isArray(value)) {
    return `[${value.join(",")}]`;
  }
  if (typeof value === "number") {
    return `0x${value.toString(16)}`;
  }
  return String(value ?? "");
}

document.getElementById("run-form").addEventListener("submit", runSimulation);
document.getElementById("create-session-button").addEventListener("click", createSession);
document.getElementById("step-session-button").addEventListener("click", stepSession);
document.getElementById("run-session-button").addEventListener("click", runSession);
document.getElementById("stop-session-button").addEventListener("click", stopSession);
renderResult({
  status: "idle",
  error: "Start pvs-http and submit a job.",
  uart_output: "",
  pins: []
});
