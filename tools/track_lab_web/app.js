"use strict";

// Track Lab deliberately has no build step and no runtime dependencies.
const SLOT_COUNT = 10;
const DOCUMENT_FORMAT = "al-track-document";
const DOCUMENT_VERSION = 1;
const GAME_ID = "lamborghini.us";
const ROM_XXH3_64 = "525201d7279f34e3";
const MIN_ROW_COUNT = 2;
const MAX_ROW_COUNT = 200;
const REQUIRED_CAPABILITIES = Object.freeze({
  editable: Object.freeze(["visibility"]),
  inspect_only: Object.freeze(["segments", "anchors", "waypoints"]),
  unsupported: Object.freeze(["geometry", "collision", "new_track"]),
});
const FNV1A64_OFFSET = 0xcbf29ce484222325n;
const FNV1A64_PRIME = 0x100000001b3n;
const U64_MASK = 0xffffffffffffffffn;
const SVG_NS = "http://www.w3.org/2000/svg";

const state = {
  document: null,
  fileName: "track.json",
  rowsPath: null,
  rows: [],
  baseRows: [],
  sourceRowLengths: [],
  sourceWarnings: [],
  selectedIndex: null,
  coordinateSource: "cull",
  undoStack: [],
  redoStack: [],
  validation: { errors: [], warnings: [] },
};

const dom = {};

function cacheDom() {
  const ids = [
    "browseButton", "servedButton", "openHeaderButton", "downloadButton",
    "fileInput", "dropZone", "loadStatus", "welcome", "workspace",
    "documentState", "formatBadge", "trackMetadata", "segmentCount",
    "waypointCount", "modifiedCount", "sourceCull", "sourceAi",
    "segmentJump", "jumpButton", "capabilities", "trackMap", "mapFrame",
    "routeLayer", "edgeLayer", "nodeLayer", "mapEmpty", "mapTooltip",
    "coordinateBadge", "coordinateWarning", "inspectorEmpty", "inspectorContent", "selectedIndex",
    "rowState", "rowSlots", "resetRowButton", "undoButton", "redoButton",
    "segmentTab", "waypointTab", "rawTab", "segmentMetadata",
    "waypointMetadata", "rawMetadata", "validationPanel", "validationTitle",
    "validationSummary", "validationIcon", "validationDetails",
    "validationToggle", "validationList", "toastRegion",
  ];
  for (const id of ids) dom[id] = document.getElementById(id);
}

function initialize() {
  cacheDom();
  buildSlotInputs();
  bindEvents();
}

function bindEvents() {
  dom.browseButton.addEventListener("click", openFilePicker);
  dom.openHeaderButton.addEventListener("click", openFilePicker);
  dom.dropZone.addEventListener("click", openFilePicker);
  dom.servedButton.addEventListener("click", () => loadServedDocument({ silentMissing: false }));
  dom.downloadButton.addEventListener("click", downloadDocument);
  dom.fileInput.addEventListener("change", handleFileSelection);
  dom.jumpButton.addEventListener("click", jumpToSegment);
  dom.segmentJump.addEventListener("keydown", (event) => {
    if (event.key === "Enter") jumpToSegment();
  });
  dom.sourceCull.addEventListener("change", handleCoordinateSourceChange);
  dom.sourceAi.addEventListener("change", handleCoordinateSourceChange);
  dom.resetRowButton.addEventListener("click", resetSelectedRow);
  dom.undoButton.addEventListener("click", undo);
  dom.redoButton.addEventListener("click", redo);
  dom.segmentTab.addEventListener("click", () => activateMetadataTab("segment"));
  dom.waypointTab.addEventListener("click", () => activateMetadataTab("waypoint"));
  dom.rawTab.addEventListener("click", () => activateMetadataTab("raw"));

  for (const tab of [dom.segmentTab, dom.waypointTab, dom.rawTab]) {
    tab.addEventListener("keydown", handleTabKeydown);
  }

  document.addEventListener("keydown", handleGlobalShortcut);
  document.addEventListener("dragenter", handleDocumentDrag);
  document.addEventListener("dragover", handleDocumentDrag);
  document.addEventListener("dragleave", handleDocumentDragLeave);
  document.addEventListener("drop", handleDocumentDrop);
}

function buildSlotInputs() {
  const fragment = document.createDocumentFragment();
  for (let slot = 0; slot < SLOT_COUNT; slot += 1) {
    const item = document.createElement("li");
    const label = document.createElement("label");
    const input = document.createElement("input");
    const position = String(slot + 1).padStart(2, "0");

    label.htmlFor = `pvsSlot${slot}`;
    label.textContent = position;
    input.id = `pvsSlot${slot}`;
    input.type = "number";
    input.inputMode = "numeric";
    input.step = "1";
    input.min = "-1";
    input.placeholder = "hole";
    input.dataset.slot = String(slot);
    input.setAttribute("aria-label", `Visibility slot ${slot + 1}`);
    input.addEventListener("input", handleSlotDraft);
    input.addEventListener("change", handleSlotCommit);

    item.append(label, input);
    fragment.append(item);
  }
  dom.rowSlots.replaceChildren(fragment);
}

function openFilePicker() {
  dom.fileInput.value = "";
  dom.fileInput.click();
}

async function handleFileSelection(event) {
  const [file] = event.target.files || [];
  if (file) await loadFile(file);
}

async function loadFile(file) {
  setLoadStatus(`Reading ${file.name}…`);
  try {
    const text = await file.text();
    const parsed = JSON.parse(text);
    loadDocument(parsed, file.name);
  } catch (error) {
    reportLoadError(`Could not open ${file.name}: ${friendlyError(error)}`);
  }
}

async function loadServedDocument({ silentMissing }) {
  if (location.protocol !== "http:" && location.protocol !== "https:") {
    reportLoadError("/track.json is available only when Track Lab is served over HTTP.");
    return;
  }

  if (!silentMissing) setLoadStatus("Loading /track.json…");
  try {
    const response = await fetch("/track.json", {
      cache: "no-store",
      headers: { Accept: "application/json" },
    });
    if (!response.ok) {
      if (silentMissing && response.status === 404) return;
      throw new Error(`HTTP ${response.status} ${response.statusText}`.trim());
    }
    const parsed = await response.json();
    loadDocument(parsed, "track.json");
  } catch (error) {
    if (!silentMissing) reportLoadError(`Could not load /track.json: ${friendlyError(error)}`);
  }
}

function loadDocument(input, fileName) {
  if (!isPlainObject(input)) throw new Error("the JSON root must be an object");

  const documentCopy = deepClone(input);
  const rowInfo = resolveRows(documentCopy);
  if (!rowInfo) throw new Error("no visibility.rows, visibility.base_rows, or visibility_rows table was found");

  const baseSource = resolveBaseRows(documentCopy, rowInfo.rows);
  state.document = documentCopy;
  state.fileName = fileName || "track.json";
  state.rowsPath = rowInfo.path;
  state.sourceRowLengths = rowInfo.rows.map((row) => Array.isArray(row) ? row.length : -1);
  state.sourceWarnings = [];
  state.rows = rowInfo.rows.map((row, index) => normalizeRow(row, index, "working"));
  state.baseRows = state.rows.map((row, index) => {
    const baseRow = index < baseSource.length ? baseSource[index] : row;
    return normalizeRow(baseRow, index, "base");
  });
  state.selectedIndex = state.rows.length ? 0 : null;
  state.coordinateSource = dom.sourceAi.checked ? "ai" : "cull";
  state.undoStack = [];
  state.redoStack = [];

  dom.segmentJump.max = String(Math.max(0, state.rows.length - 1));
  dom.welcome.hidden = true;
  dom.workspace.hidden = false;
  dom.validationPanel.hidden = false;
  setLoadStatus(`Loaded ${fileName}.`);
  renderEverything();
  showToast(`Loaded ${fileName}: ${state.rows.length} visibility rows.`, "success");
}

function resolveRows(trackDocument) {
  if (Array.isArray(trackDocument?.visibility?.rows)) {
    return { rows: trackDocument.visibility.rows, path: "visibility.rows" };
  }
  if (Array.isArray(trackDocument?.visibility?.base_rows)) {
    return { rows: trackDocument.visibility.base_rows, path: "visibility.base_rows" };
  }
  if (Array.isArray(trackDocument?.visibility_rows)) {
    return { rows: trackDocument.visibility_rows, path: "visibility_rows" };
  }
  return null;
}

function resolveBaseRows(trackDocument, fallbackRows) {
  if (Array.isArray(trackDocument?.visibility?.base_rows)) return trackDocument.visibility.base_rows;
  return fallbackRows;
}

function normalizeRow(row, index, kind) {
  if (!Array.isArray(row)) {
    state.sourceWarnings.push(`${capitalize(kind)} row ${index} was not an array; it was replaced with ten holes in the working copy.`);
    return Array(SLOT_COUNT).fill(null);
  }
  const normalized = row.slice(0, SLOT_COUNT).map(normalizeLoadedSlot);
  while (normalized.length < SLOT_COUNT) normalized.push(null);
  return normalized;
}

function normalizeLoadedSlot(value) {
  if (value === null || value === undefined || value === -1) return null;
  return value;
}

function renderEverything() {
  renderDocumentOverview();
  renderMap();
  renderInspector();
  validateAndRender();
  updateHistoryButtons();
}

function renderDocumentOverview() {
  const doc = state.document;
  dom.formatBadge.textContent = `${String(doc.format || "unknown")} · v${String(doc.version ?? "?")}`;
  dom.segmentCount.textContent = String(state.rows.length);
  dom.waypointCount.textContent = String(asArray(doc.waypoints).length);
  dom.modifiedCount.textContent = String(countModifiedRows());

  const target = isPlainObject(doc.target) ? doc.target : {};
  const track = isPlainObject(doc.track) ? doc.track : {};
  const source = isPlainObject(doc.source) ? doc.source : {};
  const metadata = [
    ["Circuit", firstPresent(target.circuit, target.id, track.circuit, track.id, "—")],
    ["Name", firstPresent(target.display_name, target.name, track.display_name, track.name, "—")],
    ["PVS source", state.rowsPath || "—"],
    ["Base fingerprint", firstPresent(doc?.visibility?.base_fnv1a64, "—")],
    ["Source", firstPresent(source.name, source.rom, source.file, doc.source_name, "—")],
  ];
  renderDefinitionList(dom.trackMetadata, metadata);
  renderCapabilities();
}

function renderCapabilities() {
  const groups = [
    ["editable", "Editable"],
    ["inspect_only", "Inspect only"],
    ["unsupported", "Unsupported"],
  ];
  const capabilities = isPlainObject(state.document?.capabilities) ? state.document.capabilities : {};
  const fragment = document.createDocumentFragment();

  for (const [key, label] of groups) {
    const heading = document.createElement("h3");
    const list = document.createElement("ul");
    heading.textContent = label;
    heading.dataset.kind = key;
    list.dataset.kind = key;
    const entries = Array.isArray(capabilities[key]) ? capabilities[key] : [];
    if (!entries.length) {
      const item = document.createElement("li");
      item.textContent = key === "editable" && state.rowsPath ? state.rowsPath : "Not declared";
      list.append(item);
    } else {
      for (const entry of entries) {
        const item = document.createElement("li");
        item.textContent = primitiveLabel(entry);
        list.append(item);
      }
    }
    fragment.append(heading, list);
  }
  dom.capabilities.replaceChildren(fragment);
}

function handleCoordinateSourceChange(event) {
  if (!event.target.checked) return;
  state.coordinateSource = event.target.value;
  renderMap();
}

function coordinateForIndex(index, source = state.coordinateSource) {
  if (source === "ai") {
    const waypoint = itemAtIndex(asArray(state.document?.waypoints), index);
    return waypointCoordinate(waypoint);
  }
  const segment = itemAtIndex(asArray(state.document?.segments), index);
  return cullCoordinate(segment);
}

function waypointCoordinate(waypoint) {
  if (!waypoint) return null;
  const decoded = isPlainObject(waypoint.decoded) ? waypoint.decoded : {};
  const coordinateObjects = [
    decoded.coordinate,
    decoded.coordinates,
    decoded.position,
    decoded.waypoint,
    waypoint.coordinate,
    waypoint.coordinates,
    waypoint.position,
  ];
  for (const candidate of coordinateObjects) {
    const pair = coordinatePair(candidate);
    if (pair) return pair;
  }
  return finitePair(
    firstFinite(decoded.x, decoded.plane_a, decoded.plane_x, waypoint.x, waypoint.plane_a, waypoint.plane_x),
    firstFinite(decoded.z, decoded.plane_b, decoded.plane_z, waypoint.z, waypoint.plane_b, waypoint.plane_z),
  );
}

function cullCoordinate(segment) {
  if (!segment) return null;
  const decoded = isPlainObject(segment.decoded) ? segment.decoded : {};
  const coordinateObjects = [
    decoded.cull_anchor,
    decoded.cullAnchor,
    decoded.auxiliary_cull_anchor,
    decoded.auxiliary_point,
    decoded.anchor,
    decoded.point,
    segment.cull_anchor,
    segment.cullAnchor,
    segment.auxiliary_cull_anchor,
    segment.anchor,
  ];
  for (const candidate of coordinateObjects) {
    const pair = coordinatePair(candidate);
    if (pair) return pair;
  }
  return finitePair(
    firstFinite(decoded.cull_anchor_x, decoded.anchor_x, decoded.point_x, segment.cull_anchor_x, segment.anchor_x),
    firstFinite(decoded.cull_anchor_z, decoded.anchor_z, decoded.point_z, segment.cull_anchor_z, segment.anchor_z),
  );
}

function coordinatePair(candidate) {
  if (Array.isArray(candidate) && candidate.length >= 2) {
    return finitePair(candidate[0], candidate[1]);
  }
  if (!isPlainObject(candidate)) return null;
  return finitePair(
    firstFinite(candidate.world_x, candidate.x, candidate.plane_a, candidate.a, candidate[0]),
    firstFinite(candidate.world_z, candidate.z, candidate.y, candidate.plane_b, candidate.b, candidate[1]),
  );
}

function finitePair(x, z) {
  return Number.isFinite(Number(x)) && Number.isFinite(Number(z))
    ? { x: Number(x), z: Number(z) }
    : null;
}

function renderMap() {
  if (!state.document) return;
  const points = [];
  for (let index = 0; index < state.rows.length; index += 1) {
    const coordinate = coordinateForIndex(index);
    if (coordinate) points.push({ index, ...coordinate });
  }

  const showingWaypointHypothesis = state.coordinateSource === "ai";
  const sourceName = showingWaypointHypothesis ? "Waypoint hypothesis" : "Cull anchors";
  dom.coordinateBadge.textContent = `${sourceName} · independently fitted`;
  if (showingWaypointHypothesis) {
    const exportedWarning = state.document?.waypoints?.assumption_warning;
    dom.coordinateWarning.textContent = typeof exportedWarning === "string" && exportedWarning.trim()
      ? `${exportedWarning} The index-order guide is inferred and intentionally left open.`
      : "These records are an inspection hypothesis. Their true extent is unproven, and the inferred index-order guide is intentionally left open.";
    dom.coordinateWarning.hidden = false;
  } else {
    dom.coordinateWarning.textContent = "";
    dom.coordinateWarning.hidden = true;
  }
  dom.mapEmpty.hidden = points.length > 0;
  dom.trackMap.setAttribute("aria-label", `${sourceName}: ${points.length} of ${state.rows.length} segments have decoded coordinates.`);
  dom.routeLayer.replaceChildren();
  dom.edgeLayer.replaceChildren();
  dom.nodeLayer.replaceChildren();

  if (!points.length) return;
  const fitted = fitPoints(points);
  const pointByIndex = new Map(fitted.map((point) => [point.index, point]));
  drawRoute(fitted, !showingWaypointHypothesis);
  drawPvsEdges(pointByIndex);
  drawNodes(fitted);
}

function fitPoints(points) {
  const left = 68;
  const right = 932;
  const top = 62;
  const bottom = 638;
  const xs = points.map((point) => point.x);
  const zs = points.map((point) => point.z);
  const minX = Math.min(...xs);
  const maxX = Math.max(...xs);
  const minZ = Math.min(...zs);
  const maxZ = Math.max(...zs);
  const spanX = maxX - minX;
  const spanZ = maxZ - minZ;
  const scaleX = spanX ? (right - left) / spanX : Number.POSITIVE_INFINITY;
  const scaleZ = spanZ ? (bottom - top) / spanZ : Number.POSITIVE_INFINITY;
  const scale = Number.isFinite(Math.min(scaleX, scaleZ)) ? Math.min(scaleX, scaleZ) : 1;
  const usedWidth = spanX * scale;
  const usedHeight = spanZ * scale;
  const offsetX = left + ((right - left) - usedWidth) / 2;
  const offsetY = top + ((bottom - top) - usedHeight) / 2;

  return points.map((point) => ({
    ...point,
    sx: offsetX + (point.x - minX) * scale,
    sy: offsetY + usedHeight - (point.z - minZ) * scale,
  }));
}

function drawRoute(points, closeLoop) {
  if (points.length < 2) return;
  const ordered = [...points].sort((a, b) => a.index - b.index);
  const commands = ordered.map((point, index) => `${index ? "L" : "M"}${round(point.sx)},${round(point.sy)}`);
  if (closeLoop && ordered.length > 2) commands.push("Z");
  const path = svgElement("path", { d: commands.join(" "), class: "route-line" });
  dom.routeLayer.append(path);
}

function drawPvsEdges(pointByIndex) {
  if (state.selectedIndex === null) return;
  const from = pointByIndex.get(state.selectedIndex);
  if (!from) return;
  const row = state.rows[state.selectedIndex] || [];
  row.forEach((targetIndex, slot) => {
    if (!Number.isInteger(targetIndex) || targetIndex < 0) return;
    const to = pointByIndex.get(targetIndex);
    if (!to) return;
    const path = svgElement("path", {
      d: edgePath(from, to, slot),
      class: "pvs-edge",
      "data-slot": String(slot),
    });
    dom.edgeLayer.append(path);
  });
}

function edgePath(from, to, slot) {
  if (from.index === to.index) {
    const radius = 23 + (slot % 3) * 6;
    return `M${round(from.sx + 8)},${round(from.sy - 8)} C${round(from.sx + radius)},${round(from.sy - radius)} ${round(from.sx - radius)},${round(from.sy - radius)} ${round(from.sx - 8)},${round(from.sy - 8)}`;
  }
  const dx = to.sx - from.sx;
  const dy = to.sy - from.sy;
  const length = Math.max(Math.hypot(dx, dy), 1);
  const bend = ((slot % 3) - 1) * Math.min(34, length * .1);
  const midpointX = (from.sx + to.sx) / 2 - (dy / length) * bend;
  const midpointY = (from.sy + to.sy) / 2 + (dx / length) * bend;
  const start = shortenLine(from.sx, from.sy, midpointX, midpointY, 14);
  const end = shortenLine(to.sx, to.sy, midpointX, midpointY, 17);
  return `M${round(start.x)},${round(start.y)} Q${round(midpointX)},${round(midpointY)} ${round(end.x)},${round(end.y)}`;
}

function shortenLine(x, y, towardX, towardY, amount) {
  const dx = towardX - x;
  const dy = towardY - y;
  const length = Math.max(Math.hypot(dx, dy), 1);
  return { x: x + (dx / length) * amount, y: y + (dy / length) * amount };
}

function drawNodes(points) {
  const referenced = state.selectedIndex === null
    ? new Set()
    : new Set((state.rows[state.selectedIndex] || []).filter(Number.isInteger));

  for (const point of points) {
    const group = svgElement("g", {
      class: "segment-node",
      transform: `translate(${round(point.sx)} ${round(point.sy)})`,
      tabindex: "0",
      role: "button",
      "aria-label": nodeAriaLabel(point),
      "data-index": String(point.index),
      "data-selected": String(point.index === state.selectedIndex),
      "data-referenced": String(referenced.has(point.index)),
      "data-modified": String(isRowModified(point.index)),
    });
    const title = svgElement("title");
    title.textContent = `Segment ${point.index}: (${formatNumber(point.x)}, ${formatNumber(point.z)})`;
    const hit = svgElement("circle", { r: "23", class: "node-hit" });
    const dot = svgElement("circle", { r: point.index === state.selectedIndex ? "13" : "10", class: "node-dot" });
    const label = svgElement("text", { y: "4.5", class: "node-label" });
    label.textContent = String(point.index);
    group.append(title, hit, dot, label);
    if (isRowModified(point.index)) {
      group.append(svgElement("circle", { cx: "11", cy: "-11", r: "4", class: "node-modified" }));
    }
    group.addEventListener("click", (event) => handleNodeSelection(point.index, event.shiftKey));
    group.addEventListener("keydown", (event) => handleNodeKeydown(event, point.index, points));
    group.addEventListener("pointerenter", (event) => showMapTooltip(event, point));
    group.addEventListener("pointermove", (event) => showMapTooltip(event, point));
    group.addEventListener("pointerleave", hideMapTooltip);
    group.addEventListener("focus", () => showFocusedMapTooltip(group, point));
    group.addEventListener("blur", hideMapTooltip);
    dom.nodeLayer.append(group);
  }
}

function nodeAriaLabel(point) {
  const selected = point.index === state.selectedIndex ? ", selected" : "";
  const modified = isRowModified(point.index) ? ", visibility row modified" : "";
  return `Segment ${point.index}${selected}${modified}; coordinate ${formatNumber(point.x)}, ${formatNumber(point.z)}. Press Enter to select.`;
}

function handleNodeSelection(index, shiftKey) {
  if (shiftKey && state.selectedIndex !== null && index !== state.selectedIndex) {
    addReferenceToFirstHole(index);
    return;
  }
  selectSegment(index, { focusInspector: false });
}

function handleNodeKeydown(event, index, points) {
  if (event.key === "Enter" || event.key === " ") {
    event.preventDefault();
    handleNodeSelection(index, event.shiftKey);
    return;
  }
  if (!["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"].includes(event.key)) return;
  event.preventDefault();
  const ordered = [...points].sort((a, b) => a.index - b.index);
  const at = ordered.findIndex((point) => point.index === index);
  const backwards = event.key === "ArrowLeft" || event.key === "ArrowUp";
  const next = ordered[(at + (backwards ? -1 : 1) + ordered.length) % ordered.length];
  const nextNode = dom.nodeLayer.querySelector(`[data-index="${next.index}"]`);
  nextNode?.focus();
}

function showMapTooltip(event, point) {
  const frame = dom.mapFrame.getBoundingClientRect();
  dom.mapTooltip.textContent = `#${point.index}  x ${formatNumber(point.x)}  z ${formatNumber(point.z)}`;
  dom.mapTooltip.style.left = `${Math.min(event.clientX - frame.left, frame.width - 190)}px`;
  dom.mapTooltip.style.top = `${Math.max(25, event.clientY - frame.top)}px`;
  dom.mapTooltip.hidden = false;
}

function showFocusedMapTooltip(node, point) {
  const nodeRect = node.getBoundingClientRect();
  const frame = dom.mapFrame.getBoundingClientRect();
  showMapTooltip({
    clientX: nodeRect.left + nodeRect.width / 2,
    clientY: nodeRect.top + nodeRect.height / 2,
  }, point);
  dom.mapTooltip.style.left = `${Math.min(nodeRect.left - frame.left + nodeRect.width, frame.width - 190)}px`;
}

function hideMapTooltip() {
  dom.mapTooltip.hidden = true;
}

function addReferenceToFirstHole(targetIndex) {
  const rowIndex = state.selectedIndex;
  const row = state.rows[rowIndex];
  if (!row) return;
  if (row.includes(targetIndex)) {
    showToast(`Segment ${targetIndex} is already present in row ${rowIndex}.`);
    return;
  }
  const hole = row.findIndex((value) => value === null || value === -1);
  if (hole < 0) {
    showToast(`Row ${rowIndex} has no empty PVS slot.`, "error");
    return;
  }
  const next = [...row];
  next[hole] = targetIndex;
  commitRow(rowIndex, next, `Add segment ${targetIndex} to slot ${hole + 1}`);
  showToast(`Added segment ${targetIndex} to row ${rowIndex}, slot ${hole + 1}.`, "success");
}

function selectSegment(index, { focusInspector = false } = {}) {
  if (!Number.isInteger(index) || index < 0 || index >= state.rows.length) {
    showToast(`Segment must be between 0 and ${Math.max(0, state.rows.length - 1)}.`, "error");
    return;
  }
  state.selectedIndex = index;
  dom.segmentJump.value = String(index);
  renderMap();
  renderInspector();
  if (focusInspector) dom.rowSlots.querySelector("input")?.focus();
}

function jumpToSegment() {
  const value = Number(dom.segmentJump.value);
  if (!Number.isInteger(value)) {
    showToast("Enter an integer segment index.", "error");
    dom.segmentJump.focus();
    return;
  }
  selectSegment(value, { focusInspector: false });
  const node = dom.nodeLayer.querySelector(`[data-index="${value}"]`);
  if (node) node.focus();
  else showToast(`Segment ${value} has no decoded ${state.coordinateSource === "ai" ? "AI waypoint" : "cull-anchor"} coordinate; its row is still selected.`);
}

function renderInspector() {
  const index = state.selectedIndex;
  const hasSelection = index !== null && Boolean(state.rows[index]);
  dom.inspectorEmpty.hidden = hasSelection;
  dom.inspectorContent.hidden = !hasSelection;
  if (!hasSelection) return;

  dom.selectedIndex.textContent = String(index);
  const modified = isRowModified(index);
  dom.rowState.textContent = modified ? "Modified" : "Stock";
  dom.rowState.dataset.modified = String(modified);

  const inputs = dom.rowSlots.querySelectorAll("input");
  state.rows[index].forEach((value, slot) => {
    const input = inputs[slot];
    input.max = String(Math.max(0, state.rows.length - 1));
    input.value = value === null || value === -1 ? "" : String(value);
    input.dataset.hole = String(value === null || value === -1);
    updateSlotValidity(input, value);
  });

  renderSelectionMetadata(index);
}

function handleSlotDraft(event) {
  const input = event.currentTarget;
  const parsed = parseSlotInput(input.value);
  input.dataset.hole = String(parsed.kind === "hole");
  if (parsed.kind === "syntax") {
    input.setCustomValidity("Enter an integer segment index, -1, or leave blank for a hole.");
    input.setAttribute("aria-invalid", "true");
  } else {
    input.setCustomValidity("");
    input.setAttribute("aria-invalid", parsed.kind === "value" && !isValidSlotValue(parsed.value) ? "true" : "false");
  }
}

function handleSlotCommit(event) {
  const input = event.currentTarget;
  const parsed = parseSlotInput(input.value);
  if (parsed.kind === "syntax") {
    input.reportValidity();
    return;
  }
  const slot = Number(input.dataset.slot);
  const rowIndex = state.selectedIndex;
  if (rowIndex === null || !state.rows[rowIndex]) return;
  const next = [...state.rows[rowIndex]];
  next[slot] = parsed.kind === "hole" ? null : parsed.value;
  commitRow(rowIndex, next, `Edit row ${rowIndex}, slot ${slot + 1}`);
}

function parseSlotInput(raw) {
  const trimmed = String(raw).trim();
  if (trimmed === "" || trimmed === "-1") return { kind: "hole", value: null };
  if (!/^-?\d+$/.test(trimmed)) return { kind: "syntax", value: null };
  return { kind: "value", value: Number(trimmed) };
}

function isValidSlotValue(value) {
  return value === null || value === -1 || (Number.isInteger(value) && value >= 0 && value < state.rows.length);
}

function updateSlotValidity(input, value) {
  const valid = isValidSlotValue(value);
  input.setCustomValidity(valid ? "" : `Use -1/a blank for a hole, or a segment index from 0 to ${Math.max(0, state.rows.length - 1)}.`);
  input.setAttribute("aria-invalid", String(!valid));
  input.title = valid ? "" : input.validationMessage;
}

function commitRow(index, nextRow, description) {
  const before = [...state.rows[index]];
  const after = normalizeWorkingRow(nextRow);
  if (rowsEqual(before, after)) {
    renderInspector();
    return;
  }
  state.rows[index] = after;
  state.undoStack.push({ index, before, after: [...after], description });
  state.redoStack = [];
  afterRowMutation(index);
}

function normalizeWorkingRow(row) {
  const output = row.slice(0, SLOT_COUNT).map((value) => value === -1 || value === undefined ? null : value);
  while (output.length < SLOT_COUNT) output.push(null);
  return output;
}

function resetSelectedRow() {
  const index = state.selectedIndex;
  if (index === null) return;
  const base = state.baseRows[index] || Array(SLOT_COUNT).fill(null);
  if (rowsEqual(state.rows[index], base)) {
    showToast(`Row ${index} already matches its stock base row.`);
    return;
  }
  commitRow(index, [...base], `Reset row ${index}`);
  showToast(`Row ${index} reset to stock base values.`, "success");
}

function undo() {
  const action = state.undoStack.pop();
  if (!action) return;
  state.rows[action.index] = [...action.before];
  state.redoStack.push(action);
  state.selectedIndex = action.index;
  afterRowMutation(action.index);
  showToast(`Undid: ${action.description}.`);
}

function redo() {
  const action = state.redoStack.pop();
  if (!action) return;
  state.rows[action.index] = [...action.after];
  state.undoStack.push(action);
  state.selectedIndex = action.index;
  afterRowMutation(action.index);
  showToast(`Redid: ${action.description}.`);
}

function afterRowMutation() {
  renderDocumentOverview();
  renderMap();
  renderInspector();
  validateAndRender();
  updateHistoryButtons();
}

function updateHistoryButtons() {
  dom.undoButton.disabled = state.undoStack.length === 0;
  dom.redoButton.disabled = state.redoStack.length === 0;
  dom.undoButton.title = state.undoStack.length
    ? `Undo: ${state.undoStack[state.undoStack.length - 1].description} (Ctrl/Command+Z)`
    : "Nothing to undo";
  dom.redoButton.title = state.redoStack.length
    ? `Redo: ${state.redoStack[state.redoStack.length - 1].description} (Ctrl/Command+Shift+Z)`
    : "Nothing to redo";
}

function countModifiedRows() {
  return state.rows.reduce((count, row, index) => count + (isRowModified(index) ? 1 : 0), 0);
}

function isRowModified(index) {
  return !rowsEqual(state.rows[index], state.baseRows[index] || Array(SLOT_COUNT).fill(null));
}

function rowsEqual(left, right) {
  if (!Array.isArray(left) || !Array.isArray(right) || left.length !== right.length) return false;
  return left.every((value, index) => normalizeComparisonSlot(value) === normalizeComparisonSlot(right[index]));
}

function normalizeComparisonSlot(value) {
  return value === null || value === undefined || value === -1 ? null : value;
}

function renderSelectionMetadata(index) {
  const segment = itemAtIndex(asArray(state.document?.segments), index);
  const waypoint = itemAtIndex(asArray(state.document?.waypoints), index);
  const decodedSegment = segment && isPlainObject(segment.decoded)
    ? segment.decoded
    : omitKeys(segment, ["raw", "raw_words", "raw_bytes", "decoded"]);
  const decodedWaypoint = waypoint && isPlainObject(waypoint.decoded)
    ? waypoint.decoded
    : omitKeys(waypoint, ["raw", "raw_words", "raw_bytes", "decoded"]);

  renderFlatMetadata(dom.segmentMetadata, decodedSegment, "No decoded segment metadata was exported for this index.");
  renderFlatMetadata(dom.waypointMetadata, decodedWaypoint, "No provisional waypoint metadata was exported for this index.");

  const raw = {};
  if (segment) {
    if (segment.raw !== undefined) raw.segment = segment.raw;
    if (segment.raw_be_hex !== undefined) raw.segment_be_hex = segment.raw_be_hex;
    if (segment.raw_words !== undefined) raw.segment_words = segment.raw_words;
    if (segment.raw_bytes !== undefined) raw.segment_bytes = segment.raw_bytes;
  }
  if (waypoint) {
    if (waypoint.raw !== undefined) raw.waypoint = waypoint.raw;
    if (waypoint.raw_be_hex !== undefined) raw.waypoint_be_hex = waypoint.raw_be_hex;
    if (waypoint.raw_words !== undefined) raw.waypoint_words = waypoint.raw_words;
    if (waypoint.raw_bytes !== undefined) raw.waypoint_bytes = waypoint.raw_bytes;
  }
  dom.rawMetadata.textContent = Object.keys(raw).length
    ? JSON.stringify(raw, null, 2)
    : "No raw segment or waypoint metadata was exported for this index.";
}

function renderFlatMetadata(container, value, emptyMessage) {
  const entries = flattenMetadata(value);
  if (!entries.length) {
    const empty = document.createElement("p");
    empty.className = "empty-data";
    empty.textContent = emptyMessage;
    container.replaceChildren(empty);
    return;
  }
  const list = document.createElement("dl");
  for (const [key, entry] of entries) {
    const term = document.createElement("dt");
    const detail = document.createElement("dd");
    term.textContent = key;
    detail.textContent = primitiveLabel(entry);
    list.append(term, detail);
  }
  container.replaceChildren(list);
}

function flattenMetadata(value, prefix = "", depth = 0) {
  if (!isPlainObject(value)) return [];
  const output = [];
  for (const [key, entry] of Object.entries(value)) {
    const label = prefix ? `${prefix}.${key}` : key;
    if (isPlainObject(entry) && depth < 1) output.push(...flattenMetadata(entry, label, depth + 1));
    else output.push([label, entry]);
  }
  return output.slice(0, 40);
}

function activateMetadataTab(name) {
  const names = ["segment", "waypoint", "raw"];
  for (const tabName of names) {
    const active = tabName === name;
    dom[`${tabName}Tab`].setAttribute("aria-selected", String(active));
    dom[`${tabName}Metadata`].hidden = !active;
    dom[`${tabName}Tab`].tabIndex = active ? 0 : -1;
  }
}

function handleTabKeydown(event) {
  if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
  event.preventDefault();
  const tabs = [dom.segmentTab, dom.waypointTab, dom.rawTab];
  const current = tabs.indexOf(event.currentTarget);
  const direction = event.key === "ArrowRight" ? 1 : -1;
  const next = tabs[(current + direction + tabs.length) % tabs.length];
  next.click();
  next.focus();
}

function validateAndRender() {
  state.validation = validateDocument();
  const { errors, warnings } = state.validation;
  const modified = countModifiedRows();
  const level = errors.length ? "error" : warnings.length ? "warning" : "valid";

  dom.validationPanel.dataset.level = level;
  dom.validationIcon.textContent = errors.length ? "×" : warnings.length ? "!" : "✓";
  dom.validationTitle.textContent = errors.length
    ? `${errors.length} validation ${errors.length === 1 ? "error" : "errors"}`
    : warnings.length
      ? `Valid with ${warnings.length} ${warnings.length === 1 ? "warning" : "warnings"}`
      : "Document valid";
  dom.validationSummary.textContent = errors.length
    ? "Fix errors before downloading an edited document."
    : warnings.length
      ? "The working visibility table can be downloaded; review the notes below."
      : "All editable visibility rows pass the v1 checks.";
  dom.validationToggle.textContent = errors.length || warnings.length ? "View findings" : "View checks";
  renderValidationList(errors, warnings);

  dom.downloadButton.disabled = errors.length > 0;
  dom.documentState.dataset.state = errors.length ? "invalid" : modified ? "modified" : "valid";
  dom.documentState.textContent = errors.length ? "Needs attention" : modified ? `${modified} row${modified === 1 ? "" : "s"} changed` : "Valid · stock";

  if (state.selectedIndex !== null) {
    const inputs = dom.rowSlots.querySelectorAll("input");
    state.rows[state.selectedIndex].forEach((value, index) => updateSlotValidity(inputs[index], value));
  }
}

function validateDocument() {
  const errors = validateTrackDocumentEvidence(state.document);
  const warnings = [...state.sourceWarnings];

  state.sourceRowLengths.forEach((length, index) => {
    if (length < 0) warnings.push(`Visibility row ${index} was not an array; the working copy uses ten holes.`);
    else if (length !== SLOT_COUNT) warnings.push(`Visibility row ${index} had ${length} slots; the working copy was ${length < SLOT_COUNT ? "padded" : "truncated"} to ${SLOT_COUNT}.`);
  });

  state.rows.forEach((row, rowIndex) => {
    if (!Array.isArray(row) || row.length !== SLOT_COUNT) {
      errors.push(`Visibility row ${rowIndex} must contain exactly ${SLOT_COUNT} slots.`);
      return;
    }
    const seen = new Set();
    row.forEach((value, slot) => {
      if (value === null || value === -1) return;
      if (!Number.isInteger(value)) {
        errors.push(`Row ${rowIndex}, slot ${slot + 1}: ${primitiveLabel(value)} is not an integer or hole.`);
        return;
      }
      if (value < 0 || value >= state.rows.length) {
        errors.push(`Row ${rowIndex}, slot ${slot + 1}: segment ${value} is outside 0–${Math.max(0, state.rows.length - 1)}.`);
        return;
      }
      if (seen.has(value)) warnings.push(`Row ${rowIndex} references segment ${value} more than once.`);
      seen.add(value);
    });
  });

  if (state.baseRows.length !== state.rows.length) {
    warnings.push(`The base table has ${state.baseRows.length} rows while the working table has ${state.rows.length}.`);
  }
  return { errors: unique(errors), warnings: unique(warnings) };
}

function validateTrackDocumentEvidence(doc) {
  const errors = [];
  if (!isPlainObject(doc)) return ["the JSON root must be an object."];

  if (doc.format !== DOCUMENT_FORMAT) {
    errors.push(`format must be "${DOCUMENT_FORMAT}" (found ${primitiveLabel(doc.format)}).`);
  }
  if (doc.version !== DOCUMENT_VERSION || !Number.isInteger(doc.version)) {
    errors.push(`version must be integer ${DOCUMENT_VERSION} (found ${primitiveLabel(doc.version)}).`);
  }

  validateTargetEvidence(doc.target, errors);
  validateCapabilityEvidence(doc.capabilities, errors);

  const visibility = doc.visibility;
  if (!isPlainObject(visibility)) {
    errors.push("visibility must be an object.");
    return unique(errors);
  }
  validateKnownFields(
    visibility,
    ["row_count", "slots_per_row", "base_fnv1a64", "base_rows", "raw_base_rows", "rows"],
    "visibility",
    errors,
  );

  const rowCount = visibility.row_count;
  const validRowCount = Number.isInteger(rowCount)
    && rowCount >= MIN_ROW_COUNT
    && rowCount <= MAX_ROW_COUNT;
  if (!validRowCount) {
    errors.push(`visibility.row_count must be an integer in ${MIN_ROW_COUNT}..${MAX_ROW_COUNT}.`);
  }
  if (visibility.slots_per_row !== SLOT_COUNT || !Number.isInteger(visibility.slots_per_row)) {
    errors.push(`visibility.slots_per_row must be integer ${SLOT_COUNT}.`);
  }

  const expectedRows = validRowCount ? rowCount : null;
  const baseRows = validateSemanticRowsEvidence(
    visibility.base_rows,
    expectedRows,
    "visibility.base_rows",
    errors,
  );
  validateSemanticRowsEvidence(
    visibility.rows,
    expectedRows,
    "visibility.rows",
    errors,
  );

  let rawBaseRows = null;
  if (visibility.raw_base_rows === undefined) {
    if (baseRows) {
      rawBaseRows = baseRows.map((row) => row.map((value) => value === null ? -1 : value));
    }
  } else {
    rawBaseRows = validateRawRowsEvidence(
      visibility.raw_base_rows,
      expectedRows,
      errors,
    );
  }

  if (baseRows && rawBaseRows) {
    for (let row = 0; row < baseRows.length; row += 1) {
      for (let slot = 0; slot < SLOT_COUNT; slot += 1) {
        const semanticRaw = rawBaseRows[row][slot] < 0 ? null : rawBaseRows[row][slot];
        if (baseRows[row][slot] !== semanticRaw) {
          errors.push(`visibility.base_rows[${row}][${slot}] disagrees with visibility.raw_base_rows.`);
        }
      }
    }
  }

  const storedHash = visibility.base_fnv1a64;
  if (typeof storedHash !== "string" || !/^[0-9a-f]{16}$/.test(storedHash)) {
    errors.push("visibility.base_fnv1a64 must be exactly 16 lowercase hexadecimal digits.");
  } else if (rawBaseRows) {
    const calculatedHash = fnv1a64Pvs(rawBaseRows);
    if (storedHash !== calculatedHash) {
      errors.push(`visibility baseline hash mismatch: document says ${storedHash}, rows fingerprint as ${calculatedHash}.`);
    }
  }

  return unique(errors);
}

function validateTargetEvidence(target, errors) {
  if (!isPlainObject(target)) {
    errors.push("target must be an object.");
    return;
  }
  validateKnownFields(target, ["game_id", "rom_xxh3_64", "circuit"], "target", errors);
  if (target.game_id !== GAME_ID) {
    errors.push(`target.game_id must be "${GAME_ID}".`);
  }
  if (target.rom_xxh3_64 !== ROM_XXH3_64) {
    errors.push(`target.rom_xxh3_64 must be "${ROM_XXH3_64}".`);
  }
  if (!Number.isInteger(target.circuit) || target.circuit < 0 || target.circuit > 5) {
    errors.push("target.circuit must be an integer in 0..5.");
  }
}

function validateCapabilityEvidence(capabilities, errors) {
  if (!isPlainObject(capabilities)) {
    errors.push("capabilities must be an object.");
    return;
  }
  validateKnownFields(capabilities, Object.keys(REQUIRED_CAPABILITIES), "capabilities", errors);
  for (const [name, expected] of Object.entries(REQUIRED_CAPABILITIES)) {
    const actual = capabilities[name];
    if (!Array.isArray(actual) || actual.some((value) => typeof value !== "string")) {
      errors.push(`capabilities.${name} must be an array containing ${expected.join(", ")}.`);
      continue;
    }
    const actualSet = new Set(actual);
    if (actualSet.size !== actual.length
        || actualSet.size !== expected.length
        || expected.some((value) => !actualSet.has(value))) {
      errors.push(`capabilities.${name} must declare exactly: ${expected.join(", ")}.`);
    }
  }
}

function validateKnownFields(value, allowed, where, errors) {
  const allowedSet = new Set(allowed);
  const unknown = Object.keys(value).filter((key) => !allowedSet.has(key));
  if (unknown.length) errors.push(`${where} has unknown field(s): ${unknown.sort().join(", ")}.`);
}

function validateSemanticRowsEvidence(value, rowCount, where, errors) {
  if (!Array.isArray(value)) {
    errors.push(`${where} must be an array.`);
    return null;
  }
  let valid = true;
  if (rowCount !== null && value.length !== rowCount) {
    errors.push(`${where} has ${value.length} rows; expected ${rowCount}.`);
    valid = false;
  }
  value.forEach((candidate, rowIndex) => {
    if (!Array.isArray(candidate)) {
      errors.push(`${where}[${rowIndex}] must be an array.`);
      valid = false;
      return;
    }
    if (candidate.length !== SLOT_COUNT) {
      errors.push(`${where}[${rowIndex}] has ${candidate.length} slots; expected ${SLOT_COUNT}.`);
      valid = false;
    }
    candidate.forEach((item, slot) => {
      const validReference = Number.isInteger(item)
        && item >= 0
        && rowCount !== null
        && item < rowCount;
      if (item !== null && !validReference) {
        const range = rowCount === null ? "the declared row range" : `0..${rowCount - 1}`;
        errors.push(`${where}[${rowIndex}][${slot}] must be null or a segment index in ${range}.`);
        valid = false;
      }
    });
  });
  return valid ? value : null;
}

function validateRawRowsEvidence(value, rowCount, errors) {
  const where = "visibility.raw_base_rows";
  if (!Array.isArray(value)) {
    errors.push(`${where} must be an array.`);
    return null;
  }
  let valid = true;
  if (rowCount !== null && value.length !== rowCount) {
    errors.push(`${where} has ${value.length} rows; expected ${rowCount}.`);
    valid = false;
  }
  value.forEach((candidate, rowIndex) => {
    if (!Array.isArray(candidate)) {
      errors.push(`${where}[${rowIndex}] must be an array.`);
      valid = false;
      return;
    }
    if (candidate.length !== SLOT_COUNT) {
      errors.push(`${where}[${rowIndex}] has ${candidate.length} slots; expected ${SLOT_COUNT}.`);
      valid = false;
    }
    candidate.forEach((item, slot) => {
      const isS16 = Number.isInteger(item) && item >= -0x8000 && item <= 0x7fff;
      const inSegmentRange = rowCount !== null && item < rowCount;
      if (!isS16 || !inSegmentRange) {
        errors.push(`${where}[${rowIndex}][${slot}] must be an s16 below the declared row count.`);
        valid = false;
      }
    });
  });
  return valid ? value : null;
}

function fnv1a64Pvs(rows) {
  let result = FNV1A64_OFFSET;
  for (const row of rows) {
    for (const value of row) {
      const encoded = value & 0xffff;
      for (const byte of [encoded >>> 8, encoded & 0xff]) {
        result ^= BigInt(byte);
        result = (result * FNV1A64_PRIME) & U64_MASK;
      }
    }
  }
  return result.toString(16).padStart(16, "0");
}

function renderValidationList(errors, warnings) {
  const fragment = document.createDocumentFragment();
  const findings = [
    ...errors.map((text) => ({ level: "error", text })),
    ...warnings.map((text) => ({ level: "warning", text })),
  ];
  if (!findings.length) {
    findings.push(
      { level: "ok", text: `Format and version identify ${DOCUMENT_FORMAT} v${DOCUMENT_VERSION}.` },
      { level: "ok", text: `Every visibility row has exactly ${SLOT_COUNT} slots.` },
      { level: "ok", text: "All references are integer segment indices in range; holes are canonical null values." },
    );
  }
  for (const finding of findings) {
    const item = document.createElement("li");
    item.dataset.level = finding.level;
    item.textContent = finding.text;
    fragment.append(item);
  }
  dom.validationList.replaceChildren(fragment);
}

function downloadDocument() {
  if (!state.document || state.validation.errors.length) return;
  const output = deepClone(state.document);
  const canonicalRows = state.rows.map((row) => row.map((value) => value === null || value === -1 ? null : value));
  output.visibility.rows = canonicalRows;
  const json = `${JSON.stringify(sortJson(output), null, 2)}\n`;
  const blob = new Blob([json], { type: "application/json;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = editedFileName(state.fileName, output);
  document.body.append(anchor);
  anchor.click();
  anchor.remove();
  setTimeout(() => URL.revokeObjectURL(url), 0);
  showToast(`Downloaded ${anchor.download}. JSON only; no ROM or game files were changed.`, "success");
}

function editedFileName(original, output) {
  const base = String(original || "track.json").replace(/\.json$/i, "");
  const circuit = firstPresent(output?.target?.circuit, output?.track?.circuit, output?.track?.id, "");
  const suffix = circuit !== "" && !base.toLowerCase().includes(String(circuit).toLowerCase()) ? `-${circuit}` : "";
  const safeSuffix = suffix ? `-${sanitizeFilePart(String(circuit))}` : "";
  return `${sanitizeFilePart(base)}${safeSuffix}.edited.json`;
}

function sortJson(value) {
  if (Array.isArray(value)) return value.map(sortJson);
  if (!isPlainObject(value)) return value;
  return Object.keys(value).sort().reduce((result, key) => {
    result[key] = sortJson(value[key]);
    return result;
  }, {});
}

function handleGlobalShortcut(event) {
  if (!(event.ctrlKey || event.metaKey) || event.altKey) return;
  const key = event.key.toLowerCase();
  if (key === "o") {
    event.preventDefault();
    openFilePicker();
  } else if (key === "s") {
    event.preventDefault();
    if (!dom.downloadButton.disabled) downloadDocument();
  } else if (key === "z") {
    event.preventDefault();
    if (event.shiftKey) redo();
    else undo();
  } else if (key === "y") {
    event.preventDefault();
    redo();
  }
}

function handleDocumentDrag(event) {
  if (!hasDraggedFiles(event)) return;
  event.preventDefault();
  if (event.dataTransfer) event.dataTransfer.dropEffect = "copy";
  dom.dropZone.dataset.dragging = "true";
}

function handleDocumentDragLeave(event) {
  if (event.relatedTarget === null) delete dom.dropZone.dataset.dragging;
}

function handleDocumentDrop(event) {
  if (!hasDraggedFiles(event)) return;
  event.preventDefault();
  delete dom.dropZone.dataset.dragging;
  const files = [...(event.dataTransfer?.files || [])];
  const file = files.find((candidate) => candidate.name.toLowerCase().endsWith(".json")) || files[0];
  if (file) loadFile(file);
}

function hasDraggedFiles(event) {
  return [...(event.dataTransfer?.types || [])].includes("Files");
}

function setLoadStatus(message, isError = false) {
  dom.loadStatus.textContent = message;
  dom.loadStatus.dataset.error = String(isError);
}

function reportLoadError(message) {
  setLoadStatus(message, true);
  showToast(message, "error");
}

function showToast(message, kind = "info") {
  const toast = document.createElement("div");
  toast.className = "toast";
  toast.dataset.kind = kind;
  toast.textContent = message;
  dom.toastRegion.append(toast);
  window.setTimeout(() => toast.remove(), 4200);
}

function renderDefinitionList(container, entries) {
  const fragment = document.createDocumentFragment();
  for (const [label, value] of entries) {
    const term = document.createElement("dt");
    const detail = document.createElement("dd");
    term.textContent = label;
    detail.textContent = primitiveLabel(value);
    fragment.append(term, detail);
  }
  container.replaceChildren(fragment);
}

function itemAtIndex(items, index) {
  return items.find((item, fallbackIndex) => itemIndex(item, fallbackIndex) === index) || null;
}

function itemIndex(item, fallbackIndex) {
  if (!isPlainObject(item)) return fallbackIndex;
  const candidate = firstPresent(item.index, item.segment_index, item.waypoint_index, item.id);
  const numeric = Number(candidate);
  return Number.isInteger(numeric) ? numeric : fallbackIndex;
}

function asArray(value) {
  if (Array.isArray(value)) return value;
  if (Array.isArray(value?.records)) return value.records;
  if (Array.isArray(value?.items)) return value.items;
  return [];
}

function omitKeys(value, keys) {
  if (!isPlainObject(value)) return null;
  return Object.fromEntries(Object.entries(value).filter(([key]) => !keys.includes(key)));
}

function firstFinite(...values) {
  for (const value of values) {
    if (value !== null && value !== "" && Number.isFinite(Number(value))) return Number(value);
  }
  return undefined;
}

function firstPresent(...values) {
  for (const value of values) {
    if (value !== undefined && value !== null && value !== "") return value;
  }
  return undefined;
}

function primitiveLabel(value) {
  if (value === undefined) return "undefined";
  if (value === null) return "null";
  if (Array.isArray(value)) return value.map(primitiveLabel).join(", ") || "[]";
  if (isPlainObject(value)) return JSON.stringify(value);
  return String(value);
}

function isPlainObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function deepClone(value) {
  return JSON.parse(JSON.stringify(value));
}

function unique(values) {
  return [...new Set(values)];
}

function svgElement(name, attributes = {}) {
  const element = document.createElementNS(SVG_NS, name);
  for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, value);
  return element;
}

function formatNumber(value) {
  return Number(value).toLocaleString(undefined, { maximumFractionDigits: 2, useGrouping: false });
}

function round(value) {
  return Math.round(value * 10) / 10;
}

function sanitizeFilePart(value) {
  return String(value).replace(/[^a-z0-9._-]+/gi, "-").replace(/^-+|-+$/g, "") || "track";
}

function capitalize(value) {
  return String(value).charAt(0).toUpperCase() + String(value).slice(1);
}

function friendlyError(error) {
  return error instanceof Error ? error.message : String(error);
}

if (typeof module !== "undefined" && module.exports) {
  module.exports = { fnv1a64Pvs, validateTrackDocumentEvidence };
}
if (typeof document !== "undefined") initialize();
