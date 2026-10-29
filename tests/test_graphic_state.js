// Run with: node --test tests/test_graphic_state.js
// Frozen pre-migration reference only. Production equivalents are tested in C++ and bridge parity tests.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { test } = require("node:test");
const vm = require("node:vm");

const filename = path.resolve(__dirname, "fixtures/legacy-viewmodels/EditorGraphicState.js");
const source = fs.readFileSync(filename, "utf8").replace(/^\s*\.pragma library\s*$/m, "");
const state = vm.runInThisContext(
    `(function () { ${source}\nreturn { nested, clone, getPath, flatten, resolvedPath, parseNumber, numberText, setPath, pathParts }; })()`,
    { filename }
);
const definitionsFilename = path.resolve(__dirname, "fixtures/legacy-viewmodels/EditorGraphicDefinitions.js");
const definitionsSource = fs.readFileSync(definitionsFilename, "utf8").replace(/^\s*\.pragma library\s*$/m, "");
const definitions = vm.runInThisContext(
    `(function () { ${definitionsSource}\nreturn { catalog, operationDefaults, defaultsFor }; })()`,
    { filename: definitionsFilename }
);

test("operation defaults reuse the first calculation without visiting the catalog again", () => {
    const first = definitions.operationDefaults("developLight");
    assert.equal(first.exposure, 0);
    assert.equal(first.contrast, 0);
    const catalog = definitions.catalog();
    const panels = catalog.panels;
    catalog.panels = { forEach() { throw new Error("Repeated render recalculated operation defaults"); } };
    try {
        assert.strictEqual(definitions.operationDefaults("developLight"), first);
    } finally {
        catalog.panels = panels;
    }
});

test("nested member edits leave cached operation defaults and original catalog arrays unchanged", () => {
    const cached = definitions.operationDefaults("developCurve");
    const cachedBefore = state.clone(cached);
    const catalogBefore = state.clone(definitions.catalog());
    assert.deepEqual(cached["master.points"], [{ input: 0, output: 0 }, { input: 1, output: 1 }]);
    const edited = state.nested(Object.assign({}, cached, { "master.points[1].output": 0.7 }));
    edited.master.points[0].input = 0.2;
    assert.equal(edited.master.points[1].output, 0.7);
    assert.deepEqual(cached, cachedBefore);
    assert.deepEqual(definitions.catalog(), catalogBefore);
    assert.strictEqual(definitions.operationDefaults("developCurve"), cached);
});

test("reset defaults omit host picker display strings and preserve actual color channels", () => {
    const defaults = definitions.defaultsFor("Effects_Plaster_EditBackground");
    assert.equal(Object.hasOwn(defaults, "background"), false);
    assert.deepEqual(defaults, { "background[0]": 1, "background[1]": 1, "background[2]": 1 });
    assert.deepEqual(state.nested(defaults), { background: [1, 1, 1] });
    const neon = definitions.operationDefaults("neonGlow");
    assert.deepEqual(neon.glowColor, [0, 0.62, 1]);
    assert.deepEqual(definitions.defaultsFor("Effects_NeonGlow").glowColor, [0, 0.62, 1]);
});

test("defaults omit image picker placeholders while retaining typed nullable fields and enum values", () => {
    assert.equal(Object.hasOwn(definitions.defaultsFor("Effects_Displace"), "map"), false);
    assert.equal(Object.hasOwn(definitions.operationDefaults("displace"), "map"), false);
    assert.equal(Object.hasOwn(definitions.defaultsFor("Effects_Displace_EditMap"), "map"), false);
    assert.equal(Object.hasOwn(definitions.defaultsFor("Effects_LensBlur_EditDepthmap"), "depthMap"), false);
    const lens = definitions.defaultsFor("Effects_LensBlur");
    assert.equal(lens.depthMap, null);
    assert.equal(lens.depth, "None");
    const all = definitions.catalog().panels;
    for (const panel of all) {
        const rows = [];
        function collect(items) { for (const row of items) { rows.push(row); if (row.rows) collect(row.rows); } }
        collect(panel.rows);
        const defaults = definitions.defaultsFor(panel.id);
        for (const row of rows.filter(row => ["hostColorPicker", "hostImagePicker"].includes(row.editor))) {
            assert.equal(Object.hasOwn(defaults, row.sdkParameter || row.parameter), false, panel.name + ": " + row.parameter);
        }
    }
});

test("clone detaches nested objects and arrays and retains optional null", () => {
    const original = { light: { exposure: 0 }, paths: [{ points: [[0, 0], [1, 1]] }], amount: null };
    const copy = state.clone(original);
    assert.deepEqual(copy, original);
    copy.light.exposure = 2;
    copy.paths[0].points[1][0] = 0.6;
    assert.deepEqual(original, { light: { exposure: 0 }, paths: [{ points: [[0, 0], [1, 1]] }], amount: null });
    assert.equal(state.clone(undefined), undefined);
    assert.equal(state.clone(null), null);
    assert.equal(state.clone(7), 7);
    assert.equal(state.clone(false), false);
});

test("nested installs parent defaults before member edits without changing defaults", () => {
    const points = [{ input: 0, output: 0 }, { input: 1, output: 1 }];
    const flat = { "master.points[1].output": 0.8, "master.points": points, "light.exposure": 1.5 };
    const before = state.clone(flat);
    const result = state.nested(flat);
    assert.deepEqual(result, {
        master: { points: [{ input: 0, output: 0 }, { input: 1, output: 0.8 }] },
        light: { exposure: 1.5 }
    });
    assert.deepEqual(flat, before);
    result.master.points[0].input = 0.2;
    assert.deepEqual(points, [{ input: 0, output: 0 }, { input: 1, output: 1 }]);
});

test("nested draft parent replaces indexed defaults and preserves an added middle curve point", () => {
    const defaults = {
        "master.points": [{ input: 0, output: 0 }, { input: 1, output: 1 }],
        "master.points[0].input": 0,
        "master.points[0].output": 0,
        "master.points[1].input": 1,
        "master.points[1].output": 1,
        "red.points": [{ input: 0, output: 0 }, { input: 1, output: 1 }]
    };
    const draft = { "master.points": [{ input: 0, output: 0 }, { input: 0.5, output: 0.65 }, { input: 1, output: 1 }] };
    const defaultsBefore = state.clone(defaults), draftBefore = state.clone(draft);
    const result = state.nested(defaults, draft);
    assert.deepEqual(result.master.points, [{ input: 0, output: 0 }, { input: 0.5, output: 0.65 }, { input: 1, output: 1 }]);
    assert.deepEqual(result.red.points, [{ input: 0, output: 0 }, { input: 1, output: 1 }]);
    assert.deepEqual(defaults, defaultsBefore);
    assert.deepEqual(draft, draftBefore);
    result.master.points[1].output = 0.75;
    assert.deepEqual(draft, draftBefore);
});

test("nested explicit draft members override the replacement parent after defaults are resolved", () => {
    const defaults = {
        "master.points": [{ input: 0, output: 0 }, { input: 1, output: 1 }],
        "master.points[1].input": 1,
        "master.points[1].output": 1,
        "grading.shadows": { hue: 0, saturation: 0, luminance: 0 },
        "grading.shadows.saturation": 25
    };
    const draft = {
        "master.points[1].output": 0.8,
        "grading.shadows.hue": 120,
        "master.points": [{ input: 0, output: 0 }, { input: 0.4, output: 0.6 }, { input: 1, output: 1 }],
        "grading.shadows": { hue: 90, luminance: 10 }
    };
    const before = state.clone(draft);
    const result = state.nested(defaults, draft);
    assert.deepEqual(result.master.points, [{ input: 0, output: 0 }, { input: 0.4, output: 0.8 }, { input: 1, output: 1 }]);
    assert.deepEqual(result.grading.shadows, { hue: 120, luminance: 10 });
    assert.equal(Object.hasOwn(result.grading.shadows, "saturation"), false);
    assert.deepEqual(draft, before);
    assert.equal(defaults["master.points[1].output"], 1);
});

test("nested edits arrays inside collection items and retains sibling data", () => {
    const defaults = [
        { points: [[0, 0], [1, 1]], width: 4 },
        { points: [[0.1, 0.2], [0.7, 0.9]], width: 7 }
    ];
    const result = state.nested({
        "paths[1].points[0][1]": 0.4,
        "paths[0].width": 5,
        paths: defaults
    });
    assert.deepEqual(result.paths, [
        { points: [[0, 0], [1, 1]], width: 5 },
        { points: [[0.1, 0.4], [0.7, 0.9]], width: 7 }
    ]);
    assert.equal(defaults[0].width, 4);
    assert.equal(defaults[1].points[0][1], 0.2);
    assert.equal(Array.isArray(result.paths[1].points[0]), true);
});

test("nested preserves null and false instead of creating a numeric zero", () => {
    assert.deepEqual(state.nested({ "options.amount": null, "options.autoMask": false }), {
        options: { amount: null, autoMask: false }
    });
});

test("resolvedPath selects every nested wildcard using the actual parent index", () => {
    const indices = { paths: 1, "paths[1].points": 2 };
    assert.equal(state.resolvedPath("paths[].points[].x", indices), "paths[1].points[2].x");
    assert.equal(state.resolvedPath("paths[].points[][1]", indices), "paths[1].points[2][1]");
    assert.equal(state.resolvedPath("paths[0].points[].x", { "paths[0].points": 3 }), "paths[0].points[3].x");
    assert.equal(state.resolvedPath("samples[].r", {}), "samples[0].r");
    assert.equal(state.resolvedPath("grading.shadows.hue", indices), "grading.shadows.hue");
    assert.deepEqual(indices, { paths: 1, "paths[1].points": 2 });
});

test("resolved nested selection changes only the chosen collection member", () => {
    const paths = [
        { points: [{ x: 0, y: 0 }] },
        { points: [{ x: 0.2, y: 0.4 }, { x: 0.8, y: 0.9 }] }
    ];
    const key = state.resolvedPath("paths[].points[].y", { paths: 1, "paths[1].points": 1 });
    const result = state.nested({ paths, [key]: 0.6 });
    assert.equal(result.paths[1].points[1].y, 0.6);
    assert.equal(result.paths[1].points[0].y, 0.4);
    assert.equal(result.paths[0].points[0].y, 0);
    assert.equal(paths[1].points[1].y, 0.9);
});

test("getPath reads indexed arrays and keeps missing and null distinct", () => {
    const object = { master: { points: [{ input: 0, output: 0 }, { input: 1, output: 0.75 }] }, amount: null };
    assert.equal(state.getPath(object, "master.points[1].output"), 0.75);
    assert.equal(state.getPath(object, "master.points[0].input"), 0);
    assert.equal(state.getPath(object, "amount"), null);
    assert.equal(state.getPath(object, "amount.value"), undefined);
    assert.equal(state.getPath(object, "master.points[2].output"), undefined);
    assert.equal(state.getPath(object, "absent.field"), undefined);
});

test("flatten round-trips nested parameters and copies collection arrays", () => {
    const original = { grading: { shadows: { hue: 90, saturation: 0 } }, points: [[0, 0], [1, 1]], amount: null };
    const flat = state.flatten(original);
    assert.deepEqual(flat, { "grading.shadows.hue": 90, "grading.shadows.saturation": 0, points: [[0, 0], [1, 1]], amount: null });
    assert.deepEqual(state.nested(flat), original);
    flat.points[0][0] = 0.5;
    assert.equal(original.points[0][0], 0);
});

test("flatten applies an indexed sample prefix while preserving array inputs", () => {
    const sample = { source: [0.2, 0.3, 0.4, 1], hue: 15, range: { low: 0.1, high: 0.8 } };
    const flat = state.flatten(sample, "points[2]");
    assert.deepEqual(flat, { "points[2].source": [0.2, 0.3, 0.4, 1], "points[2].hue": 15, "points[2].range.low": 0.1, "points[2].range.high": 0.8 });
    const result = state.nested(flat);
    assert.deepEqual(state.getPath(result, "points[2]"), sample);
    result.points[2].source[0] = 0.9;
    assert.equal(sample.source[0], 0.2);
});

test("parseNumber accepts inclusive endpoints and rejects values outside them", () => {
    const row = { min: -5, max: 5, unit: "EV" };
    for (const [input, expected] of [["-5 EV", -5], ["5 EV", 5], [" +2.5 EV ", 2.5], ["0", 0], [".5 EV", 0.5], ["1e-2 EV", 0.01]]) {
        assert.equal(state.parseNumber(row, input), expected, input);
    }
    for (const input of ["-5.0001 EV", "5.0001 EV"]) assert.equal(state.parseNumber(row, input), undefined, input);
});

test("parseNumber honors exclusive bounds and normalized visible-alpha limits", () => {
    const alpha = { min: 0, max: 1, minExclusive: true };
    assert.equal(state.parseNumber(alpha, "0"), undefined);
    assert.equal(state.parseNumber(alpha, "0.000001"), 0.000001);
    assert.equal(state.parseNumber(alpha, "1"), 1);
    const unitInterval = { min: 0, max: 1, minExclusive: true, maxExclusive: true };
    assert.equal(state.parseNumber(unitInterval, "1"), undefined);
    assert.equal(state.parseNumber(unitInterval, "0.999999"), 0.999999);
    assert.equal(state.parseNumber(unitInterval, "0"), undefined);
});

test("parseNumber supports unset optional settings without treating zero as unset", () => {
    assert.equal(state.parseNumber({ optional: true, min: -100, max: 100 }, "Default"), null);
    assert.equal(state.parseNumber({ optional: true }, " dEfAuLt "), null);
    assert.equal(state.parseNumber({ initial: null }, "Default"), null);
    assert.equal(state.parseNumber({ optional: true, min: -100, max: 100 }, "0"), 0);
    assert.equal(state.parseNumber({ initial: 5 }, "Default"), undefined);
    assert.equal(state.parseNumber({ optional: true }, ""), undefined);
    assert.equal(state.parseNumber({ optional: true }, "null"), undefined);
});

test("parseNumber requires complete numeric text and a matching unit", () => {
    const row = { min: -100, max: 100, unit: "%" };
    assert.equal(state.parseNumber(row, "25%"), 25);
    assert.equal(state.parseNumber(row, "25 %"), 25);
    for (const input of ["", " ", "NaN", "Infinity", "-Infinity", "1e309", "12px", "12EV", "12%%", "12%more", "1,25%", "0x10", "true", "null", "--2", "1e", "2 + 2", "1.2.3", "25% %"]) {
        assert.equal(state.parseNumber(row, input), undefined, input);
    }
    assert.equal(state.parseNumber({ unit: "°", min: -180, max: 180 }, "-90°"), -90);
    assert.equal(state.parseNumber({ unit: "px", min: 0, max: 100 }, "30px"), 30);
});

test("parseNumber rejects fractional integers and respects signed int32 endpoints", () => {
    const row = { integer: true, min: -2147483648, max: 2147483647, unit: "px" };
    assert.equal(state.parseNumber(row, "-2147483648 px"), -2147483648);
    assert.equal(state.parseNumber(row, "2147483647 px"), 2147483647);
    assert.equal(state.parseNumber(row, "2.0 px"), 2);
    assert.equal(state.parseNumber(row, "2e1 px"), 20);
    for (const input of ["2147483648 px", "-2147483649 px", "0.5 px", "-1.1 px"]) {
        assert.equal(state.parseNumber(row, input), undefined, input);
    }
});

test("numberText preserves the optional Default label and displays actual units", () => {
    assert.equal(state.numberText({ displayValue: "Default" }, null), "Default");
    assert.equal(state.numberText({}, null), "Default");
    assert.equal(state.numberText({ unit: "EV", step: 0.01 }, 1.5), "1.5 EV");
    assert.equal(state.numberText({ unit: "%", step: 1 }, 0), "0%");
    assert.equal(state.numberText({ unit: "°", step: 1 }, -90), "-90°");
    assert.equal(state.numberText({ unit: "px", integer: true }, 12), "12 px");
});
