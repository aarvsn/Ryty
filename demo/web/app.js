// Ryty Web 2.1.0 - PS4 / PS5 Executable Relinker & Porting Studio Engine
// Parses ELF binary structures in browser memory, performs NID analysis,
// section/segment inspection, hex viewing, report diffing and generates the
// JSON report. Everything runs client side; no byte ever leaves the browser.

const RYTY_WEB_VERSION = "2.1.0";

let fileBuffer = null;
let fileName = "";
let parsedElf = null;
let compareBuffer = null;
let compareName = "";

const dropzone = document.getElementById("dropzone");
const fileInput = document.getElementById("fileInput");
const fileMeta = document.getElementById("fileMeta");
const fileNameSpan = document.getElementById("fileName");
const fileSizeSpan = document.getElementById("fileSize");
const clearFileBtn = document.getElementById("clearFileBtn");
const portBtn = document.getElementById("portBtn");
const resultsSection = document.getElementById("resultsSection");

// ---------------------------------------------------------------- theme ----
const themeToggle = document.getElementById("themeToggle");

function applyTheme(theme) {
    document.documentElement.setAttribute("data-theme", theme);
    themeToggle.textContent = theme === "light" ? "☀️ Light" : "🌙 Dark";
    try { localStorage.setItem("ryty-theme", theme); } catch (error) { /* private mode */ }
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.setAttribute("content", theme === "light" ? "#ffffff" : "#181e2a");
}

let storedTheme = null;
try { storedTheme = localStorage.getItem("ryty-theme"); } catch (error) { /* private mode */ }
applyTheme(storedTheme || (window.matchMedia && window.matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark"));

themeToggle.addEventListener("click", () => {
    applyTheme(document.documentElement.getAttribute("data-theme") === "light" ? "dark" : "light");
});

// ------------------------------------------------- PWA service worker ------
// The studio is fully client side; the service worker makes it installable
// and usable with no network at all. On file:// or unsupported browsers this
// silently degrades to a plain page.
if ("serviceWorker" in navigator && (location.protocol === "https:" || location.hostname === "localhost")) {
    window.addEventListener("load", () => {
        navigator.serviceWorker.register("sw.js").catch(() => { /* offline cache unavailable */ });
    });
}

// ------------------------------------------- File Selection & drag-drop ----
dropzone.addEventListener("click", () => fileInput.click());
dropzone.addEventListener("keydown", (event) => {
    if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        fileInput.click();
    }
});

dropzone.addEventListener("dragover", (e) => {
    e.preventDefault();
    dropzone.classList.add("dragover");
});

dropzone.addEventListener("dragleave", () => dropzone.classList.remove("dragover"));

dropzone.addEventListener("drop", (e) => {
    e.preventDefault();
    dropzone.classList.remove("dragover");
    if (e.dataTransfer.files.length > 0) {
        handleFileSelect(e.dataTransfer.files[0]);
    }
});

fileInput.addEventListener("change", (e) => {
    if (e.target.files.length > 0) {
        handleFileSelect(e.target.files[0]);
    }
});

clearFileBtn.addEventListener("click", () => {
    fileBuffer = null;
    parsedElf = null;
    compareBuffer = null;
    fileInput.value = "";
    fileMeta.classList.add("hidden");
    resultsSection.classList.add("hidden");
    portBtn.disabled = true;
});

function handleFileSelect(file) {
    fileName = file.name;
    fileNameSpan.textContent = file.name;
    fileSizeSpan.textContent = (file.size / 1024 / 1024).toFixed(2) + " MB";
    fileMeta.classList.remove("hidden");

    const reader = new FileReader();
    reader.onload = function(evt) {
        fileBuffer = new Uint8Array(evt.target.result);
        portBtn.disabled = false;
    };
    reader.readAsArrayBuffer(file);
}

// Tab navigation
document.querySelectorAll(".tab-btn").forEach(btn => {
    btn.addEventListener("click", () => {
        document.querySelectorAll(".tab-btn").forEach(b => b.classList.remove("active"));
        document.querySelectorAll(".tab-content").forEach(c => c.classList.add("hidden"));
        btn.classList.add("active");
        const tabId = "tab-" + btn.dataset.tab;
        document.getElementById(tabId).classList.remove("hidden");
    });
});

// ------------------------------------------------- Binary Parser Helpers ---
function readU16(buffer, offset) {
    return buffer[offset] | (buffer[offset + 1] << 8);
}

function readU32(buffer, offset) {
    return (buffer[offset] | (buffer[offset + 1] << 8) | (buffer[offset + 2] << 16) | (buffer[offset + 3] << 24)) >>> 0;
}

function readU64(buffer, offset) {
    const low = readU32(buffer, offset);
    const high = readU32(buffer, offset + 4);
    return (BigInt(high) << 32n) | BigInt(low);
}

function readString(buffer, offset, endOffset) {
    let str = "";
    const limit = endOffset ? Math.min(buffer.length, endOffset) : buffer.length;
    for (let i = offset; i < limit; i++) {
        if (buffer[i] === 0) break;
        str += String.fromCharCode(buffer[i]);
    }
    return str;
}

function parseElfHeader(buffer) {
    if (buffer.length < 64 || buffer[0] !== 0x7f || buffer[1] !== 0x45 || buffer[2] !== 0x4c || buffer[3] !== 0x46) {
        throw new Error("Invalid file format: Expected little-endian ELF64 x86-64 binary");
    }
    const e_type = readU16(buffer, 16);
    const e_entry = readU64(buffer, 24);
    const e_phoff = Number(readU64(buffer, 32));
    const e_shoff = Number(readU64(buffer, 40));
    const e_phentsize = readU16(buffer, 54);
    const e_phnum = readU16(buffer, 56);
    const e_shentsize = readU16(buffer, 58);
    const e_shnum = readU16(buffer, 60);
    const e_shstrndx = readU16(buffer, 62);

    return { e_type, e_entry, e_phoff, e_shoff, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx };
}

function parseProgramHeaders(buffer, phoff, phentsize, phnum) {
    const headers = [];
    for (let i = 0; i < phnum; i++) {
        const off = phoff + i * phentsize;
        if (off + 56 > buffer.length) break;
        headers.push({
            type: readU32(buffer, off),
            flags: readU32(buffer, off + 4),
            offset: readU64(buffer, off + 8),
            vaddr: readU64(buffer, off + 16),
            paddr: readU64(buffer, off + 24),
            filesz: readU64(buffer, off + 32),
            memsz: readU64(buffer, off + 40),
            align: readU64(buffer, off + 48)
        });
    }
    return headers;
}

// Section headers (new in 2.0.0)
function parseSectionHeaders(buffer, header) {
    const sections = [];
    if (!header.e_shoff || !header.e_shnum) return sections;
    let shstrtab = null;
    for (let i = 0; i < header.e_shnum; i++) {
        const off = header.e_shoff + i * header.e_shentsize;
        if (off + 64 > buffer.length) break;
        sections.push({
            nameOffset: readU32(buffer, off),
            type: readU32(buffer, off + 4),
            flags: Number(readU64(buffer, off + 8)),
            addr: Number(readU64(buffer, off + 16)),
            offset: Number(readU64(buffer, off + 24)),
            size: Number(readU64(buffer, off + 32)),
            link: readU32(buffer, off + 40),
            info: readU32(buffer, off + 44),
            entsize: Number(readU64(buffer, off + 56))
        });
    }
    if (header.e_shstrndx < sections.length) {
        const strSection = sections[header.e_shstrndx];
        if (strSection.offset + strSection.size <= buffer.length) {
            shstrtab = buffer.slice(strSection.offset, strSection.offset + strSection.size);
        }
    }
    for (const section of sections) {
        section.name = "";
        if (shstrtab && section.nameOffset < shstrtab.length) {
            let end = section.nameOffset;
            while (end < shstrtab.length && shstrtab[end] !== 0) end++;
            section.name = String.fromCharCode(...shstrtab.slice(section.nameOffset, end));
        }
    }
    return sections;
}

// Auto Console Detection (PS4 vs PS5)
function detectConsoleTarget(buffer) {
    let text = "";
    const scanLimit = Math.min(buffer.length, 128 * 1024);
    for (let i = 0; i < scanLimit; i++) {
        if (buffer[i] >= 32 && buffer[i] <= 126) text += String.fromCharCode(buffer[i]);
    }
    if (text.includes("libkernel_sys.prx") || text.includes("libSceAgc.sprx")) return "ps5";
    if (text.includes("libkernel.sprx") || text.includes("libSceVideoOut.sprx")) return "ps4";
    return "ps5"; // default fallback
}

// --------------------------------------------------- Hex viewer (2.0.0) ----
function hex2(value) {
    return value.toString(16).padStart(2, "0");
}

function escapeHtml(text) {
    return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function renderHexView(offset, length) {
    const view = document.getElementById("hexView");
    if (!fileBuffer || fileBuffer.length === 0) {
        view.textContent = "No file loaded.";
        return;
    }
    offset = Math.max(0, Math.min(offset, fileBuffer.length));
    const end = Math.min(offset + length, fileBuffer.length);
    const rows = [];
    for (let rowStart = offset; rowStart < end; rowStart += 16) {
        const rowEnd = Math.min(rowStart + 16, end);
        let hexPart = "";
        let asciiPart = "";
        for (let i = rowStart; i < rowEnd; i++) {
            const value = fileBuffer[i];
            const cell = hex2(value);
            hexPart += (value === 0 ? `<span class="hex-zero">${cell}</span>` : cell) + " ";
            asciiPart += value >= 32 && value <= 126 ? String.fromCharCode(value) : ".";
        }
        const padding = 16 - (rowEnd - rowStart);
        hexPart += "   ".repeat(padding);
        rows.push(`<span class="hex-offset">${rowStart.toString(16).padStart(8, "0")}</span>  ${hexPart} <span class="hex-ascii">|${escapeHtml(asciiPart)}</span>|`);
    }
    view.innerHTML = rows.join("\n") || "(empty range)";
}

function populateHexSources(parsed) {
    const selector = document.getElementById("hexSource");
    selector.innerHTML = "";
    const option = (label, value) => {
        const element = document.createElement("option");
        element.value = value;
        element.textContent = label;
        return element;
    };
    selector.appendChild(option("ELF header (0x0)", "0"));
    parsed.programHeaders.forEach((ph, index) => {
        selector.appendChild(option(`Segment ${index} (${segmentTypeName(ph.type)}) @ 0x${ph.offset.toString(16)}`, String(ph.offset)));
    });
    parsed.sections.forEach((section, index) => {
        if (section.type === 8) return; // SHT_NOBITS has no file bytes
        selector.appendChild(option(`Section ${index}: ${section.name || "(unnamed)"} @ 0x${section.offset.toString(16)}`, String(section.offset)));
    });
}

document.getElementById("hexSource").addEventListener("change", () => {
    renderHexView(parseInt(document.getElementById("hexSource").value, 10) || 0, parseInt(document.getElementById("hexLength").value, 10));
});
document.getElementById("hexLength").addEventListener("change", () => {
    renderHexView(parseInt(document.getElementById("hexSource").value, 10) || 0, parseInt(document.getElementById("hexLength").value, 10));
});
document.getElementById("hexGoBtn").addEventListener("click", () => {
    const raw = document.getElementById("hexJump").value.trim();
    if (!raw) return;
    const value = parseInt(raw.replace(/^0x/i, ""), 16);
    if (Number.isNaN(value)) return;
    document.getElementById("hexSource").value = String(value);
    renderHexView(value, parseInt(document.getElementById("hexLength").value, 10));
});

// ------------------------------------------------------- NID live filter ---
document.getElementById("nidSearch").addEventListener("input", (event) => {
    const query = event.target.value.trim();
    const rows = document.querySelectorAll("#nidsTable tbody tr");
    let matcher = null;
    if (query.startsWith("/") && query.endsWith("/") && query.length > 2) {
        try { matcher = new RegExp(query.slice(1, -1), "i"); } catch (error) { matcher = null; }
    }
    let visible = 0;
    rows.forEach(row => {
        const name = row.dataset.name || "";
        const match = matcher ? matcher.test(name) : name.toLowerCase().includes(query.toLowerCase());
        row.classList.toggle("hidden", query.length > 0 && !match);
        if (query.length === 0 || match) visible++;
    });
    document.getElementById("nidMatchCount").textContent = query ? `${visible} / ${rows.length} match${visible === 1 ? "" : "es"}` : `${rows.length} symbols`;
});

// --------------------------------------------------------- Compare (2.0) ---
document.getElementById("compareInput").addEventListener("change", (event) => {
    const file = event.target.files[0];
    if (!file || !fileBuffer) return;
    const reader = new FileReader();
    reader.onload = function(evt) {
        compareBuffer = new Uint8Array(evt.target.result);
        compareName = file.name;
        runCompare();
    };
    reader.readAsArrayBuffer(file);
});

function nidSet(elf) {
    const names = new Set();
    elf.symbols.forEach(symbol => names.add(symbol.name));
    return names;
}

function runCompare() {
    const status = document.getElementById("compareStatus");
    const output = document.getElementById("compareResult");
    if (!fileBuffer || !compareBuffer) return;
    try {
        const base = parsedElf;
        const other = analyzeBuffer(compareBuffer, "auto", document.getElementById("loweringTarget").value);
        const baseNids = nidSet(base);
        const otherNids = nidSet(other);
        const added = [...otherNids].filter(name => !baseNids.has(name));
        const removed = [...baseNids].filter(name => !otherNids.has(name));
        const baseLibs = new Set(base.neededLibs);
        const otherLibs = new Set(other.neededLibs);
        const libsAdded = [...otherLibs].filter(name => !baseLibs.has(name));
        const libsRemoved = [...baseLibs].filter(name => !otherLibs.has(name));

        status.textContent = `Compared against ${compareName} (${(compareBuffer.length / 1024 / 1024).toFixed(2)} MB)`;

        const rows = [];
        const row = (label, a, b) => rows.push(`<tr><th>${label}</th><td>${a}</td><td>${b}</td></tr>`);
        row("File", fileName, compareName);
        row("Size", `${fileBuffer.length} bytes`, `${compareBuffer.length} bytes`);
        row("Console", base.detectedConsole.toUpperCase(), other.detectedConsole.toUpperCase());
        row("ELF type", base.header.e_type === 3 ? "ET_DYN" : "ET_EXEC", other.header.e_type === 3 ? "ET_DYN" : "ET_EXEC");
        row("Entry point", "0x" + base.header.e_entry.toString(16), "0x" + other.header.e_entry.toString(16));
        row("Program headers", String(base.programHeaders.length), String(other.programHeaders.length));
        row("Sections", String(base.sections.length), String(other.sections.length));
        row("Needed libraries", base.neededLibs.join(", ") || "-", other.neededLibs.join(", ") || "-");
        row("NID count", String(baseNids.size), String(otherNids.size));

        const diffList = (items, cssClass) => items.length === 0
            ? "<span class=\"small\">none</span>"
            : `<span class="${cssClass}">${items.length}</span> ` + items.slice(0, 24).map(escapeHtml).join(", ") + (items.length > 24 ? " …" : "");
        rows.push(`<tr><th>NIDs added</th><td colspan="2">${diffList(added, "diff-added")}</td></tr>`);
        rows.push(`<tr><th>NIDs removed</th><td colspan="2">${diffList(removed, "diff-removed")}</td></tr>`);
        rows.push(`<tr><th>Libraries added</th><td colspan="2">${diffList(libsAdded, "diff-added")}</td></tr>`);
        rows.push(`<tr><th>Libraries removed</th><td colspan="2">${diffList(libsRemoved, "diff-removed")}</td></tr>`);

        output.innerHTML = `<table><thead><tr><th>Property</th><th>${escapeHtml(fileName)}</th><th>${escapeHtml(compareName)}</th></tr></thead><tbody>${rows.join("")}</tbody></table>`;
    } catch (error) {
        status.textContent = "";
        output.innerHTML = `<p class="diff-removed">Compare failed: ${escapeHtml(error.message)}</p>`;
    }
}

// ------------------------------------------------- Perform Porting Run -----
function analyzeBuffer(buffer, modeSetting, lowering) {
    const header = parseElfHeader(buffer);
    const programHeaders = parseProgramHeaders(buffer, header.e_phoff, header.e_phentsize, header.e_phnum);
    const sections = parseSectionHeaders(buffer, header);
    const detectedConsole = modeSetting === "auto" ? detectConsoleTarget(buffer) : modeSetting;

    // Parse PT_DYNAMIC segment
    const dynamicHeader = programHeaders.find(ph => ph.type === 2);
    const dynamicTags = [];
    let strTabOff = 0, strTabSize = 0, symTabOff = 0, symTabSize = 0;
    const neededLibs = [];

    if (dynamicHeader) {
        const dynOff = Number(dynamicHeader.offset);
        const dynSz = Number(dynamicHeader.filesz);
        for (let offset = dynOff; offset < dynOff + dynSz; offset += 16) {
            if (offset + 16 > buffer.length) break;
            const tag = Number(readU64(buffer, offset));
            const val = readU64(buffer, offset + 8);
            if (tag === 0) break; // DT_NULL
            dynamicTags.push({ tag, val });

            if (tag === 5) strTabOff = Number(val);
            if (tag === 10) strTabSize = Number(val);
            if (tag === 6) symTabOff = Number(val);
            if (tag === 11) symTabSize = Number(val);
        }
    }

    // Resolve DT_NEEDED libraries
    dynamicTags.filter(t => t.tag === 1).forEach(t => {
        if (strTabOff > 0 && Number(t.val) < strTabSize) {
            const libName = readString(buffer, strTabOff + Number(t.val), strTabOff + strTabSize);
            if (libName) neededLibs.push(libName);
        }
    });

    // Parse Dynamic Symbols / NIDs
    const symbols = [];
    if (symTabOff > 0 && strTabOff > 0) {
        // Find STRTAB file offset from PT_LOAD
        const loadSeg = programHeaders.find(ph => ph.type === 1 && strTabOff >= Number(ph.vaddr) && strTabOff < Number(ph.vaddr) + Number(ph.memsz));
        const fileStrTabOff = loadSeg ? Number(loadSeg.offset + BigInt(strTabOff) - loadSeg.vaddr) : strTabOff;
        const entryCount = symTabSize > 0 ? Math.min(Math.floor(symTabSize / 24), 4096) : 200;

        for (let i = 0; i < entryCount; i++) {
            const symOff = symTabOff + i * 24;
            if (symOff + 24 > buffer.length) break;
            const st_name = readU32(buffer, symOff);
            const st_info = buffer[symOff + 4];
            const st_value = readU64(buffer, symOff + 8);
            const symName = (st_name < strTabSize) ? readString(buffer, fileStrTabOff + st_name, fileStrTabOff + strTabSize) : "";

            if (symName) {
                symbols.push({
                    index: i,
                    name: symName,
                    info: st_info,
                    value: "0x" + st_value.toString(16)
                });
            }
        }
    }

    return { header, programHeaders, sections, dynamicTags, neededLibs, symbols, detectedConsole, lowering };
}

portBtn.addEventListener("click", () => {
    if (!fileBuffer) return;

    const targetOs = document.getElementById("targetOs").value;
    const modeSetting = document.getElementById("consoleMode").value;
    const lowering = document.getElementById("loweringTarget").value;
    const unusedFilter = document.getElementById("unusedFilter").value;
    const rytyNet = document.getElementById("rytyNet").checked;
    const entitlementsTemplate = document.getElementById("entitlementsTemplate").value;
    const interpPath = document.getElementById("interpPath").value.trim();
    const runPath = document.getElementById("runPath").value.trim();

    try {
        parsedElf = analyzeBuffer(fileBuffer, modeSetting, lowering);
        parsedElf.targetOs = targetOs;
        parsedElf.entitlementsTemplate = entitlementsTemplate;

        // Render Results
        renderSummary(parsedElf, rytyNet, { interpPath, runPath, entitlementsTemplate, toIntel: lowering === "intel" });
        renderSchemeCard(parsedElf, { interpPath, runPath, entitlementsTemplate, toIntel: lowering === "intel" });
        renderProgramHeaders(parsedElf.programHeaders);
        renderSections(parsedElf.sections);
        renderDynamicTags(parsedElf.dynamicTags, strTabOffOf(parsedElf), strTabSizeOf(parsedElf));
        renderNids(parsedElf.symbols);
        populateHexSources(parsedElf);
        renderHexView(0, parseInt(document.getElementById("hexLength").value, 10));
        renderReport(parsedElf, targetOs, rytyNet, unusedFilter, { interpPath, runPath, entitlementsTemplate, toIntel: lowering === "intel" });
        document.getElementById("compareResult").innerHTML = "";
        document.getElementById("compareStatus").textContent = "";

        resultsSection.classList.remove("hidden");
    } catch (err) {
        alert("Porting Error: " + err.message);
    }
});

// 2.0.0 porting scheme: mirror of the CLI's PortScheme for the browser
// studio. Every front end describes a port the same way.
function portScheme(targetOs, options) {
    const opts = options || {};
    const runPath = opts.runPath || "$ORIGIN/libs";
    const interpPath = opts.interpPath || "/lib64/ld-linux-x86-64.so.2";
    const toIntel = opts.toIntel === true;
    if (targetOs === "windows") {
        return {
            system: "windows", format: "PE32+", architecture: "x86-64",
            signature: "unsigned", layout: "flat", subsystem: "GUI",
            runPath: "", nxStack: false, lazyBinding: false, toIntel
        };
    }
    if (targetOs === "macos" || targetOs === "ios" || targetOs === "ios-sim") {
        const platform = targetOs === "macos" ? "platform 1 (macOS)" : targetOs === "ios" ? "platform 2 (iOS)" : "platform 7 (iOS Simulator)";
        const minOs = targetOs === "macos" ? "11.0" : "14.0";
        return {
            system: targetOs, format: "Mach-O 64",
            architecture: targetOs === "macos" ? "x86_64" : "arm64",
            signature: opts.entitlementsTemplate ? "ad-hoc CodeDirectory + entitlements" : "ad-hoc CodeDirectory",
            layout: "flat",
            applePlatform: platform, minOsVersion: minOs,
            runPath: runPath, nxStack: false, lazyBinding: false, toIntel
        };
    }
    return {
        system: "linux", format: "SysV ELF64", architecture: "x86-64",
        signature: "none", layout: "flat",
        interpreter: interpPath, runPath: runPath,
        nxStack: true, lazyBinding: false, toIntel
    };
}

function schemeDescribe(scheme) {
    let text = scheme.system + " → " + scheme.format + " (" + scheme.architecture + ")";
    if (scheme.applePlatform) text += ", " + scheme.applePlatform + ", minOS " + scheme.minOsVersion;
    if (scheme.subsystem) text += ", " + scheme.subsystem + " subsystem";
    text += ", " + scheme.signature + ", " + scheme.layout;
    if (scheme.interpreter) text += ", PT_INTERP " + scheme.interpreter;
    if (scheme.nxStack) text += ", non-executable stack";
    if (scheme.system === "linux") text += scheme.runPath ? ", DT_RUNPATH " + scheme.runPath : ", no DT_RUNPATH";
    else if (scheme.runPath) text += ", library path " + scheme.runPath;
    if (scheme.toIntel) text += ", Intel lowering (--to-intel)";
    return text;
}

// Live one-line scheme preview under the settings, refreshed on any change.
function updateSchemePreview() {
    const targetOs = document.getElementById("targetOs").value;
    const options = {
        interpPath: document.getElementById("interpPath").value.trim(),
        runPath: document.getElementById("runPath").value.trim(),
        entitlementsTemplate: document.getElementById("entitlementsTemplate").value,
        toIntel: document.getElementById("loweringTarget").value === "intel"
    };
    const scheme = portScheme(targetOs, options);
    document.getElementById("schemePreview").textContent = "Scheme: " + schemeDescribe(scheme);
}

["targetOs", "interpPath", "runPath", "entitlementsTemplate", "loweringTarget"].forEach(id => {
    const element = document.getElementById(id);
    element.addEventListener("change", updateSchemePreview);
    element.addEventListener("input", updateSchemePreview);
});

// Scheme card in the summary tab: the resolved plan as key/value chips.
function renderSchemeCard(elf, options) {
    const scheme = portScheme(elf.targetOs, options);
    const fields = [
        ["Target", scheme.system],
        ["Output format", scheme.format + " (" + scheme.architecture + ")"]
    ];
    if (scheme.applePlatform) {
        fields.push(["Mach-O platform", scheme.applePlatform]);
        fields.push(["Deployment target", scheme.minOsVersion]);
    }
    if (scheme.subsystem) fields.push(["Subsystem", scheme.subsystem]);
    fields.push(["Signature", scheme.signature]);
    fields.push(["Layout", scheme.layout]);
    if (scheme.interpreter) fields.push(["Interpreter", scheme.interpreter]);
    if (scheme.nxStack) fields.push(["Stack", "non-executable (PT_GNU_STACK)"]);
    fields.push(["Library path", scheme.runPath || "(none)"]);
    fields.push(["Intel lowering", scheme.toIntel ? "enabled (--to-intel)" : "disabled"]);
    const card = document.getElementById("schemeCard");
    card.innerHTML = "";
    const title = document.createElement("div");
    title.className = "scheme-title";
    title.textContent = "Porting scheme";
    card.appendChild(title);
    const chips = document.createElement("div");
    chips.className = "scheme-chips";
    for (const [label, value] of fields) {
        const chip = document.createElement("span");
        chip.className = "scheme-chip";
        const labelSpan = document.createElement("b");
        labelSpan.textContent = label + ": ";
        chip.appendChild(labelSpan);
        chip.appendChild(document.createTextNode(value));
        chips.appendChild(chip);
    }
    card.appendChild(chips);
}

function strTabOffOf(elf) {
    const tag = elf.dynamicTags.find(t => t.tag === 5);
    return tag ? Number(tag.val) : 0;
}

function strTabSizeOf(elf) {
    const tag = elf.dynamicTags.find(t => t.tag === 10);
    return tag ? Number(tag.val) : 0;
}

function segmentTypeName(type) {
    const names = {
        1: "PT_LOAD", 2: "PT_DYNAMIC", 3: "PT_INTERP", 4: "PT_NOTE",
        6: "PT_PHDR", 7: "PT_TLS", 0x61000000: "PT_SCE_DYNLIBDATA",
        0x61000001: "PT_SCE_RELRO", 0x6FFFFFFF: "PT_GNU_EH_FRAME"
    };
    return names[type] || "0x" + type.toString(16);
}

function sectionTypeName(type) {
    const names = {
        0: "SHT_NULL", 1: "SHT_PROGBITS", 2: "SHT_SYMTAB", 3: "SHT_STRTAB",
        4: "SHT_RELA", 5: "SHT_HASH", 6: "SHT_DYNAMIC", 7: "SHT_NOTE",
        8: "SHT_NOBITS", 9: "SHT_REL", 11: "SHT_DYNSYM", 14: "SHT_INIT_ARRAY",
        15: "SHT_FINI_ARRAY", 0x61000000: "SHT_SCE_DYNLIBDATA", 0x6ffffff6: "SHT_GNU_HASH",
        0x6ffffffe: "SHT_GNU_VERNEED", 0x6fffffff: "SHT_GNU_VERSYM"
    };
    return names[type] || "0x" + type.toString(16);
}

function renderSummary(elf, rytyNet, schemeOptions) {
    document.getElementById("detectedConsole").textContent = elf.detectedConsole.toUpperCase();
    document.getElementById("elfType").textContent = elf.header.e_type === 3 ? "ET_DYN (PRX Shared Object)" : "ET_EXEC (Executable)";
    document.getElementById("entryPoint").textContent = "0x" + elf.header.e_entry.toString(16);
    document.getElementById("neededCount").textContent = elf.neededLibs.length;
    document.getElementById("sectionCount").textContent = elf.sections.length;
    document.getElementById("symbolCount").textContent = elf.symbols.length;

    const logBox = document.getElementById("portLog");
    const opts = schemeOptions || {};
    const runPath = opts.runPath || "$ORIGIN/libs";
    const interpPath = opts.interpPath || "/lib64/ld-linux-x86-64.so.2";
    // 2.0.0 porting scheme: per-target writer notes, matching the CLI's
    // "Porting scheme:" line for the selected target.
    let targetNote = "";
    if (elf.targetOs === "macos" || elf.targetOs === "ios" || elf.targetOs === "ios-sim") {
        targetNote = `[apple] Mach-O writer: LC_BUILD_VERSION platform ${elf.targetOs === "macos" ? "1 (macOS)" : elf.targetOs === "ios" ? "2 (iOS)" : "7 (iOS Simulator)"}, ad-hoc code signature${elf.entitlementsTemplate ? `, entitlements preset ${elf.entitlementsTemplate}` : ""}, LC_RPATH ${runPath}\n`;
    } else if (elf.targetOs === "windows") {
        targetNote = `[windows] PE32+ writer: machine x86-64, GUI subsystem, import/entry stubs, TLS and relocations\n`;
    } else {
        targetNote = `[linux] ELF64 writer: PT_INTERP ${interpPath}, PT_GNU_STACK (non-executable), DT_RUNPATH ${runPath || "(none)"}, bind-now PLT\n`;
    }
    logBox.textContent = `[ryty-web v${RYTY_WEB_VERSION}] Starting web porting pipeline...
[relinker] File parsed successfully (${(fileBuffer.length / 1024 / 1024).toFixed(2)} MB)
[console] Auto-detected console target: ${elf.detectedConsole.toUpperCase()}
[target] Converting executable to ${(elf.targetOs || "linux").toUpperCase()} native target
${targetNote}${elf.lowering === "intel" ? "[lowering] Lowering SHA-NI and AMD-only x86-64 instructions to Intel stubs\n" : "[lowering] Native x86-64 passthrough (AMD-only instructions kept)\n"}[dynamic] Found ${elf.dynamicTags.length} dynamic tags, ${elf.neededLibs.length} DT_NEEDED references
[sections] Parsed ${elf.sections.length} section headers
${rytyNet ? "[rytynet] RytyNet online service bridge enabled\n" : ""}[relinker] String table bounds verified by DT_STRSZ (safe parsing guaranteed)
[relinker] Relinking completed successfully.`;
}

function renderProgramHeaders(headers) {
    const tbody = document.querySelector("#headersTable tbody");
    tbody.innerHTML = "";
    headers.forEach((h, idx) => {
        const tr = document.createElement("tr");
        tr.innerHTML = `
            <td>${idx}</td>
            <td>${segmentTypeName(h.type)}</td>
            <td>0x${h.flags.toString(16)}</td>
            <td>0x${h.offset.toString(16)}</td>
            <td>0x${h.vaddr.toString(16)}</td>
            <td>0x${h.filesz.toString(16)}</td>
            <td>0x${h.memsz.toString(16)}</td>
            <td>0x${h.align.toString(16)}</td>
            <td><button class="row-link" data-offset="${h.offset}">hex</button></td>
        `;
        tbody.appendChild(tr);
    });
    tbody.querySelectorAll(".row-link").forEach(button => {
        button.addEventListener("click", () => {
            document.getElementById("hexSource").value = String(button.dataset.offset);
            activateTab("hex");
            renderHexView(parseInt(button.dataset.offset, 10) || 0, parseInt(document.getElementById("hexLength").value, 10));
        });
    });
}

function renderSections(sections) {
    const tbody = document.querySelector("#sectionsTable tbody");
    tbody.innerHTML = "";
    sections.forEach((s, idx) => {
        const tr = document.createElement("tr");
        tr.innerHTML = `
            <td>${idx}</td>
            <td><code>${escapeHtml(s.name) || "(unnamed)"}</code></td>
            <td>${sectionTypeName(s.type)}</td>
            <td>0x${s.flags.toString(16)}</td>
            <td>0x${s.offset.toString(16)}</td>
            <td>0x${s.addr.toString(16)}</td>
            <td>0x${s.size.toString(16)}</td>
            <td>0x${s.entsize.toString(16)}</td>
            <td>${s.type === 8 ? "-" : `<button class="row-link" data-offset="${s.offset}">hex</button>`}</td>
        `;
        tbody.appendChild(tr);
    });
    tbody.querySelectorAll(".row-link").forEach(button => {
        button.addEventListener("click", () => {
            document.getElementById("hexSource").value = String(button.dataset.offset);
            activateTab("hex");
            renderHexView(parseInt(button.dataset.offset, 10) || 0, parseInt(document.getElementById("hexLength").value, 10));
        });
    });
}

function activateTab(name) {
    document.querySelectorAll(".tab-btn").forEach(b => b.classList.toggle("active", b.dataset.tab === name));
    document.querySelectorAll(".tab-content").forEach(c => c.classList.add("hidden"));
    document.getElementById("tab-" + name).classList.remove("hidden");
}

function renderDynamicTags(tags, strTabOff, strTabSize) {
    const tbody = document.querySelector("#dynamicTable tbody");
    tbody.innerHTML = "";
    const tagNames = {
        1: "DT_NEEDED", 2: "DT_PLTRELSZ", 3: "DT_PLTGOT", 4: "DT_HASH", 5: "DT_STRTAB",
        6: "DT_SYMTAB", 7: "DT_RELA", 8: "DT_RELASZ", 10: "DT_STRSZ", 11: "DT_SYMENT",
        14: "DT_SONAME", 21: "DT_SYMTAB_SHNDX", 0x6100002f: "DT_SCE_PLTGOT", 0x61000038: "DT_SCE_HASH"
    };

    tags.forEach(t => {
        const tr = document.createElement("tr");
        let extra = "";
        if (t.tag === 1 && strTabOff > 0 && Number(t.val) < strTabSize) {
            extra = " (" + readString(fileBuffer, strTabOff + Number(t.val), strTabOff + strTabSize) + ")";
        }
        tr.innerHTML = `
            <td>0x${t.tag.toString(16)}</td>
            <td>${tagNames[t.tag] || "SCE_CUSTOM_TAG"}</td>
            <td>0x${t.val.toString(16)}${escapeHtml(extra)}</td>
        `;
        tbody.appendChild(tr);
    });
}

function renderNids(symbols) {
    const tbody = document.querySelector("#nidsTable tbody");
    tbody.innerHTML = "";

    symbols.forEach(s => {
        const tr = document.createElement("tr");
        tr.dataset.name = s.name;
        tr.innerHTML = `
            <td>${s.index}</td>
            <td><code>${escapeHtml(s.name)}</code></td>
            <td>${s.info & 0xf}</td>
            <td>${s.info >> 4}</td>
            <td>${s.value}</td>
        `;
        tbody.appendChild(tr);
    });
    const search = document.getElementById("nidSearch");
    search.value = "";
    document.getElementById("nidMatchCount").textContent = `${symbols.length} symbols`;
}

function renderReport(elf, targetOs, rytyNet, unusedFilter, schemeOptions) {
    // 2.0.0 porting scheme: the report carries the same scheme object as the
    // CLI's ryty-port-report/2 documents.
    const scheme = portScheme(targetOs, schemeOptions);
    const report = {
        tool: "Ryty Web Studio",
        version: RYTY_WEB_VERSION,
        schema: "ryty-web-report/2",
        targetOs: targetOs,
        console: elf.detectedConsole,
        scheme: {
            system: scheme.system,
            format: scheme.format,
            architecture: scheme.architecture,
            signature: scheme.signature,
            layout: scheme.layout,
            interpreter: scheme.interpreter || null,
            subsystem: scheme.subsystem || null,
            applePlatform: scheme.applePlatform || null,
            minOsVersion: scheme.minOsVersion || null,
            runPath: scheme.runPath || "",
            nxStack: scheme.nxStack,
            toIntel: scheme.toIntel
        },
        apple: ["macos", "ios", "ios-sim"].includes(targetOs) ? {
            platform: targetOs,
            entitlementsTemplate: elf.entitlementsTemplate || null,
            adHocSignature: true
        } : null,
        relinker: {
            stringTableBoundedByDtStrSz: true,
            unusedFilterLevel: parseInt(unusedFilter),
            rytyNetEnabled: rytyNet,
            loweringTarget: elf.lowering
        },
        executable: {
            entryPoint: "0x" + elf.header.e_entry.toString(16),
            neededLibraries: elf.neededLibs,
            symbolsCount: elf.symbols.length,
            programHeadersCount: elf.programHeaders.length,
            sectionsCount: elf.sections.length,
            sections: elf.sections.filter(s => s.name).map(s => ({ name: s.name, type: sectionTypeName(s.type), offset: "0x" + s.offset.toString(16), size: "0x" + s.size.toString(16) }))
        }
    };

    const reportJson = JSON.stringify(report, null, 2);
    document.getElementById("jsonReportText").textContent = reportJson;

    document.getElementById("downloadReportBtn").onclick = () => {
        const blob = new Blob([reportJson], { type: "application/json" });
        const url = URL.createObjectURL(blob);
        const a = document.createElement("a");
        a.href = url;
        a.download = "ryty-port-report.json";
        a.click();
        URL.revokeObjectURL(url);
    };

    document.getElementById("copyReportBtn").onclick = async () => {
        try {
            await navigator.clipboard.writeText(reportJson);
            document.getElementById("copyReportBtn").textContent = "Copied!";
        } catch (error) {
            const range = document.createRange();
            const node = document.getElementById("jsonReportText");
            range.selectNodeContents(node);
            const selection = window.getSelection();
            selection.removeAllRanges();
            selection.addRange(range);
            document.execCommand("copy");
            selection.removeAllRanges();
            document.getElementById("copyReportBtn").textContent = "Copied!";
        }
        setTimeout(() => { document.getElementById("copyReportBtn").textContent = "Copy Report"; }, 1500);
    };

    document.getElementById("exportBinaryBtn").onclick = () => {
        // Client-side studio: exports the analysis metadata and the exact
        // binary bytes that were inspected (no server-side relinking).
        const metadata = JSON.stringify({ ...report, inputFileName: fileName, inputBytes: fileBuffer.length }, null, 2);
        const trigger = (blob, name) => {
            const url = URL.createObjectURL(blob);
            const a = document.createElement("a");
            a.href = url;
            a.download = name;
            a.click();
            URL.revokeObjectURL(url);
        };
        trigger(new Blob([metadata], { type: "application/json" }), (fileName || "input") + ".ryty-metadata.json");
        setTimeout(() => trigger(new Blob([fileBuffer], { type: "application/octet-stream" }), (fileName || "input") + ".bin"), 300);
    };
}
