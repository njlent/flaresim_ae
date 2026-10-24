/*
<javascriptresource>
<name>FlareSim Preset...</name>
<category>FlareSim</category>
<about>Apply a saved FlareSim preset to the active layer.</about>
</javascriptresource>
*/

// Picks a saved FlareSim preset from a list and applies it to the active
// layer. Installed into Photoshop's File > Scripts menu by
// scripts/install_ps_plugin.ps1.

#target photoshop
//@include "flaresim.jsxinc"

(function () {
    if (app.documents.length === 0) {
        alert("Open a document first.", "FlareSim");
        return;
    }
    var presets = flareSimListPresets();
    if (presets.length === 0) {
        alert("No FlareSim presets yet. Open Filter > FlareSim > FlareSim..., type a name in the Preset box and press Save.\n\n" +
              "Presets are stored in " + flareSimPresetFolder().fsName, "FlareSim");
        return;
    }

    var win = new Window("dialog", "FlareSim Preset");
    win.alignChildren = "fill";
    var row = win.add("group");
    row.add("statictext", undefined, "Preset:");
    var list = row.add("dropdownlist", undefined, presets);
    list.selection = 0;
    list.preferredSize.width = 260;
    var showDialog = win.add("checkbox", undefined, "Open the FlareSim dialog to adjust before applying");
    var buttons = win.add("group");
    buttons.alignment = "right";
    buttons.add("button", undefined, "Apply", {name: "ok"});
    buttons.add("button", undefined, "Cancel", {name: "cancel"});
    if (win.show() !== 1) {
        return;
    }

    try {
        flareSim(list.selection.text, null, showDialog.value);
    } catch (e) {
        // 8007 is "user cancelled": the filter dialog's Cancel or Esc during the render.
        if (e.number !== 8007) {
            alert("FlareSim: " + e, "FlareSim");
        }
    }
})();
