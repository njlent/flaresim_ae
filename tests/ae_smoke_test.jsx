var outputPath = "E:/projects/ae/flaresim_ae/build-ae/ae-smoke-report.txt";
var logFile = new File(outputPath);

function writeLine(message) {
    logFile.writeln(message);
}

function safeString(value) {
    if (value === undefined || value === null) {
        return "";
    }
    return String(value);
}

logFile.encoding = "UTF-8";
logFile.open("w");

try {
    writeLine("AE version: " + safeString(app.version));
    writeLine("Effects count: " + safeString(app.effects.length));

    var flareMatches = [];
    for (var i = 0; i < app.effects.length; i += 1) {
        var effect = app.effects[i];
        var displayName = safeString(effect.displayName);
        var matchName = safeString(effect.matchName);
        var categoryName = safeString(effect.categoryName);
        if (displayName.toLowerCase().indexOf("flare") !== -1 ||
            matchName.toLowerCase().indexOf("flare") !== -1) {
            flareMatches.push(displayName + " | " + matchName + " | " + categoryName);
        }
    }

    writeLine("Flare-like effects: " + safeString(flareMatches.length));
    for (var j = 0; j < flareMatches.length; j += 1) {
        writeLine(flareMatches[j]);
    }

    app.newProject();
    var project = app.project;
    writeLine("Project object: " + safeString(project));
    var comp = project.items.addComp("FlareSimAETest", 320, 180, 1.0, 1.0, 24.0);
    var solid = comp.layers.addSolid([1.0, 1.0, 1.0], "Source", 320, 180, 1.0);

    var parade = solid.property("ADBE Effect Parade");
    writeLine("Effect parade: " + safeString(parade));
    var requestedNames = [
        "FlareSim AE",
        "net.njlent.flaresim.ae"
    ];

    var added = null;
    var addedWith = "";
    for (var k = 0; k < requestedNames.length; k += 1) {
        try {
            added = parade.addProperty(requestedNames[k]);
            if (added !== null) {
                addedWith = requestedNames[k];
                break;
            }
        } catch (addError) {
            writeLine("addProperty failed for " + requestedNames[k] + ": " + safeString(addError));
        }
    }

    if (added !== null) {
        writeLine("Effect add success: " + addedWith);
    } else {
        writeLine("Effect add success: no");
    }

    var saveFile = new File("E:/projects/ae/flaresim_ae/build-ae/ae-smoke-test.aep");
    project.save(saveFile);
    writeLine("Project saved: " + saveFile.fsName);
} catch (error) {
    writeLine("ERROR: " + safeString(error));
}

logFile.close();

app.quit();
