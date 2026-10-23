// Driven by tests/ps_host/run_host_test.py, which prepends a CONFIG object:
//   CONFIG = { dir: "...", settings32: "...", settingsLdr: "...", settingsLayer: "..." }
// Each case applies FlareSim through its scripting event (no dialog), then
// saves an uncompressed TIFF for the Python side to compare.

app.displayDialogs = DialogModes.NO;
app.preferences.rulerUnits = Units.PIXELS;

// The PiPL ScopeString is the filter's scripting identifier.
var FLARESIM = stringIDToTypeID("flareSimPS");
var KEY_SETTINGS = charIDToTypeID("FsSt");
var log = [];

function applyFlareSim(settings) {
    var desc = new ActionDescriptor();
    desc.putString(KEY_SETTINGS, settings);
    return executeAction(FLARESIM, desc, DialogModes.NO);
}

function saveTiff(doc, name, transparency) {
    var options = new TiffSaveOptions();
    options.imageCompression = TIFFEncoding.NONE;
    options.layers = false;
    options.alphaChannels = false;
    options.transparency = transparency;
    options.byteOrder = ByteOrder.IBM;
    doc.saveAs(new File(CONFIG.dir + "/" + name), options, true, Extension.LOWERCASE);
}

function runCase(name, body) {
    try {
        body();
        log.push(name + ": ok");
    } catch (e) {
        log.push(name + ": FAILED " + e + (e.line ? " (line " + e.line + ")" : ""));
    }
    while (app.documents.length > CONFIG.baseDocuments) {
        app.activeDocument.close(SaveOptions.DONOTSAVECHANGES);
    }
}

runCase("flat32", function () {
    var doc = app.open(new File(CONFIG.dir + "/in32.tif"));
    log.push("flat32 bits=" + doc.bitsPerChannel);
    var reply = applyFlareSim(CONFIG.settings32);
    var recorded = reply.hasKey(KEY_SETTINGS) ? reply.getString(KEY_SETTINGS) : "";
    log.push("flat32 recorded_settings_chars=" + recorded.length);
    var file = new File(CONFIG.dir + "/recorded32.txt");
    file.encoding = "UTF-8";
    file.open("w");
    file.write(recorded);
    file.close();
    saveTiff(doc, "out32.tif", false);
});

runCase("selection32", function () {
    var doc = app.open(new File(CONFIG.dir + "/in32.tif"));
    var r = CONFIG.selection;
    doc.selection.select([[r[0], r[1]], [r[2], r[1]], [r[2], r[3]], [r[0], r[3]]]);
    applyFlareSim(CONFIG.settings32);
    doc.selection.deselect();
    saveTiff(doc, "out32_selection.tif", false);
});

runCase("large32", function () {
    var doc = app.open(new File(CONFIG.dir + "/in32_large.tif"));
    var started = new Date().getTime();
    applyFlareSim(CONFIG.settings32);
    log.push("large32 " + doc.width.as("px") + "x" + doc.height.as("px") + " ms=" + (new Date().getTime() - started));
    saveTiff(doc, "out32_large.tif", false);
});

runCase("flat16", function () {
    var doc = app.open(new File(CONFIG.dir + "/in16.tif"));
    log.push("flat16 bits=" + doc.bitsPerChannel);
    applyFlareSim(CONFIG.settingsLdr);
    saveTiff(doc, "out16.tif", false);
});

runCase("flat8", function () {
    var doc = app.open(new File(CONFIG.dir + "/in8.tif"));
    log.push("flat8 bits=" + doc.bitsPerChannel);
    applyFlareSim(CONFIG.settingsLdr);
    saveTiff(doc, "out8.tif", false);
});

runCase("smart32", function () {
    var doc = app.open(new File(CONFIG.dir + "/in32.tif"));
    executeAction(stringIDToTypeID("newPlacedLayer"), undefined, DialogModes.NO);
    log.push("smart32 layer_kind=" + doc.activeLayer.kind);
    applyFlareSim(CONFIG.settings32);
    saveTiff(doc, "out32_smart.tif", false);
});

runCase("layer32", function () {
    var doc = app.documents.add(CONFIG.width, CONFIG.height, 72, "flaresim_layer32", NewDocumentMode.RGB,
                                DocumentFill.TRANSPARENT, 1, BitsPerChannelType.THIRTYTWO);
    applyFlareSim(CONFIG.settingsLayer);
    saveTiff(doc, "out32_layer.tif", true);
});

log.join("\n");
