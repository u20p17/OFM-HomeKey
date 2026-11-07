// ETS online button handlers. Only public IDs cross KNX; setup codes stay local.
var HK_onlineErrors = ["", "Gerät beschäftigt", "Ungültige Anfrage", "ETS-Anwendung zuerst herunterladen", "Home Key nicht gestartet", "Inbetriebnahme gesperrt", "Warteschlange beschäftigt", "Speicher/Krypto nicht verfügbar", "Geräteliste geändert; erneut lesen", "Endpoint nicht gefunden", "Programmiermodus zuerst einschalten", "Identifizierung abgelaufen", "Sperrliste voll (128 Geräte); keine bestehende Sperre wurde entfernt"];
var HK_commissioningText = ["bereit / Code gespeichert", "geöffnet", "geschlossen", "nicht erlaubt", "lokalen Setup-Code zuerst setzen", "Worker beschäftigt", "Code konnte nicht gespeichert werden", "angefordert"];
var HK_authorizationText = ["deaktiviert", "USB-Berechtigungen", "ETS-Tabelle akzeptiert", "ETS-Tabelle ungültig", "Endpoint noch nicht provisioniert", "UID-ID belegt", "Speicher/Krypto nicht verfügbar"];

function HK_setOnlineStatus(device, text) {
    var parameter = device.getParameterByName("HK_OnlineStatus");
    if (parameter) parameter.value = text.substring(0, 199);
}
function HK_call(device, online, progress, data) {
    var response = BASE_invokeFunctionPropertyWrapper(165, 3, data, device, online, progress);
    if (!response || response.length < 2 || Number(response[1]) != 1) throw new Error("Home Key: Ungültige Antwort / Protokollversion");
    return response;
}
function HK_requireOk(response) {
    if (Number(response[0]) != 0) throw new Error("Home Key: " + (HK_onlineErrors[Number(response[0])] || "Unbekannter Gerätefehler"));
}
function HK_wait(progress, milliseconds) {
    var until = new Date().getTime() + milliseconds;
    while (new Date().getTime() < until) {
        if (progress.isCanceled()) throw new Error("Home Key: Abgebrochen");
    }
}
function HK_status(device, online, progress) {
    var until = new Date().getTime() + 15000;
    do {
        if (progress.isCanceled()) throw new Error("Home Key: Abgebrochen");
        var response = HK_call(device, online, progress, [0]);
        if (Number(response[0]) != 1) {
            HK_requireOk(response);
            if (response.length != 12 || response[7] > 64) throw new Error("Home Key: Ungültige Statusantwort");
            return response;
        }
        HK_wait(progress, 200);
    } while (new Date().getTime() < until);
    throw new Error("Home Key: Gerät bleibt beschäftigt; später erneut versuchen");
}
function HK_statusText(status) {
    return "Pairing: " + (status[4] ? "offen" : "geschlossen") + "; " + (HK_commissioningText[status[5]] || "unbekannt") +
        "; " + (HK_authorizationText[status[6]] || "unbekannt") + "; Endpoints: " + status[7];
}
function HK_run(device, online, progress, work) {
    try {
        online.connect();
        work();
        progress.setProgress(100);
    } catch (error) {
        HK_setOnlineStatus(device, "Fehler: " + error.message);
        throw error;
    } finally {
        try { online.disconnect(); } catch (ignored) { }
    }
}
function HK_refreshStatus(device, online, progress, context) {
    HK_run(device, online, progress, function() {
        var text = HK_statusText(HK_status(device, online, progress));
        HK_setOnlineStatus(device, text); progress.setText(text);
    });
}
function HK_openPairing(device, online, progress, context) {
    HK_run(device, online, progress, function() {
        var requested = false;
        try {
            var status = HK_status(device, online, progress);
            if (!status[2] || !status[3]) throw new Error("Home Key: Aktivierung/Inbetriebnahme zuerst im Gerät freigeben");
            HK_requireOk(HK_call(device, online, progress, [1])); requested = true;
            var until = new Date().getTime() + 15000;
            do {
                status = HK_status(device, online, progress);
                if (status[4] && status[5] == 1) {
                    var text = "Pairing für fünf Minuten geöffnet";
                    HK_setOnlineStatus(device, text); progress.setText(text); return;
                }
                if (status[5] != 7) throw new Error("Home Key: " + (HK_commissioningText[status[5]] || "Pairing fehlgeschlagen"));
                HK_wait(progress, 200);
            } while (new Date().getTime() < until);
            throw new Error("Home Key: Zeitüberschreitung beim Öffnen");
        } catch (error) {
            // Cancellation/error must also invalidate a queued late open request.
            if (requested) { try { HK_call(device, online, progress, [2]); } catch (ignored) { } }
            throw error;
        }
    });
}
function HK_closePairing(device, online, progress, context) {
    HK_run(device, online, progress, function() {
        HK_requireOk(HK_call(device, online, progress, [2]));
        var text = HK_statusText(HK_status(device, online, progress));
        HK_setOnlineStatus(device, text); progress.setText(text);
    });
}
function HK_hex(bytes, start, length) {
    var result = "";
    for (var i = start; i < start + length; i++) {
        var value = Number(bytes[i]);
        if (value < 0 || value > 255 || value % 1 != 0) throw new Error("Home Key: Ungültige ID-Daten");
        result += (value < 16 ? "0" : "") + value.toString(16);
    }
    return result;
}
function HK_readEndpoints(device, online, progress) {
    for (var attempt = 0; attempt < 3; attempt++) {
        var status = HK_status(device, online, progress);
        var generation = status.slice(8, 12), count = Number(status[7]), entries = [], seen = {}, retry = false;
        for (var index = 0; index < count; index++) {
            if (progress.isCanceled()) throw new Error("Home Key: Abgebrochen");
            var response = HK_call(device, online, progress, [3, index].concat(generation));
            if (response[0] == 1 || response[0] == 8) { retry = true; break; }
            HK_requireOk(response);
            if (response.length != 22 || response[2] != index || response[3] != count) throw new Error("Home Key: Ungültige Endpoint-Antwort");
            for (var g = 0; g < 4; g++) if (response[g + 4] != generation[g]) throw new Error("Home Key: Inkonsistente Geräteliste");
            var entry = { issuer: HK_hex(response, 8, 8), endpoint: HK_hex(response, 16, 6), index: index, generation: generation.slice(0) };
            var key = entry.issuer + "." + entry.endpoint;
            if (seen[key]) throw new Error("Home Key: Doppelte Geräte-ID");
            seen[key] = true; entries.push(entry);
            progress.setProgress(Math.floor((index + 1) * 70 / Math.max(count, 1)));
        }
        if (!retry) {
            var end = HK_status(device, online, progress);
            if (end[7] != count) retry = true;
            for (var g = 0; g < 4; g++) if (end[g + 8] != generation[g]) retry = true;
        }
        if (!retry) return entries;
        HK_wait(progress, 200);
    }
    throw new Error("Home Key: Geräteliste änderte sich während des Lesens; später erneut versuchen");
}
function HK_importEndpoints(device, online, progress, context) {
    HK_run(device, online, progress, function() {
        var entries = HK_readEndpoints(device, online, progress), free = [], existing = {}, plan = [];
        for (var row = 1; row <= 16; row++) {
            var prefix = "HK_Authorization" + (row < 10 ? "0" : "") + row;
            var fields = { enabled: device.getParameterByName(prefix + "Enabled"), issuer: device.getParameterByName(prefix + "Issuer"),
                endpoint: device.getParameterByName(prefix + "Endpoint"), action: device.getParameterByName(prefix + "AccessId"), description: device.getParameterByName(prefix + "Description") };
            if (!fields.enabled || !fields.issuer || !fields.endpoint || !fields.action || !fields.description) throw new Error("Home Key: ETS-Tabelle fehlt; passende Anwendung verwenden");
            var issuer = String(fields.issuer.value).toLowerCase(), endpoint = String(fields.endpoint.value).toLowerCase();
            if (issuer || endpoint || Number(fields.enabled.value) != 0 || Number(fields.action.value) != 0 || String(fields.description.value)) {
                if (/^[0-9a-f]{16}$/.test(issuer) && /^[0-9a-f]{12}$/.test(endpoint)) existing[issuer + "." + endpoint] = true;
            } else free.push(fields);
        }
        for (var i = 0; i < entries.length; i++) {
            var key = entries[i].issuer + "." + entries[i].endpoint;
            if (!existing[key]) { plan.push(entries[i]); existing[key] = true; }
        }
        // Capacity and the complete snapshot are checked before changing any field.
        if (plan.length > free.length) throw new Error("Home Key: Nicht genug leere Zeilen (" + plan.length + " Geräte, " + free.length + " frei); vorhandene Einträge bleiben erhalten");
        if (progress.isCanceled()) throw new Error("Home Key: Abgebrochen");
        try {
            for (var i = 0; i < plan.length; i++) {
                free[i].issuer.value = plan[i].issuer; free[i].endpoint.value = plan[i].endpoint;
                free[i].action.value = 0; free[i].enabled.value = 0;
            }
        } catch (error) {
            for (var i = 0; i < plan.length; i++) {
                try { free[i].issuer.value = ""; free[i].endpoint.value = ""; free[i].action.value = 0; free[i].enabled.value = 0; } catch (ignored) { }
            }
            throw error;
        }
        var text = plan.length + " Geräte-IDs importiert (deaktiviert). NFC-Tag-IDs vergeben, Zeilen aktivieren und herunterladen.";
        HK_setOnlineStatus(device, text); progress.setText(text);
    });
}

function HK_authorizationFields(device, row) {
    var prefix = "HK_Authorization" + (row < 10 ? "0" : "") + row;
    var fields = { enabled: device.getParameterByName(prefix + "Enabled"), issuer: device.getParameterByName(prefix + "Issuer"),
        endpoint: device.getParameterByName(prefix + "Endpoint"), action: device.getParameterByName(prefix + "AccessId"), description: device.getParameterByName(prefix + "Description") };
    if (!fields.enabled || !fields.issuer || !fields.endpoint || !fields.action || !fields.description)
        throw new Error("Home Key: Passende ETS-Anwendung mit Bezeichnungen erforderlich");
    return fields;
}
function HK_deleteAuthorization(row, device, online, progress, context) {
    var target = HK_authorizationFields(device, row);
    var issuer = String(target.issuer.value).toLowerCase(), endpoint = String(target.endpoint.value).toLowerCase();
    if (!/^[0-9a-f]{16}$/.test(issuer) || !/^[0-9a-f]{12}$/.test(endpoint)) {
        // An incomplete ETS draft has no safely addressable device to revoke.
        // Clear only the selected slot, never infer an identity from its Tag-ID
        // or touch another slot. A parameter download applies the permission
        // removal to the reader; no device keys/revocation list are changed here.
        if (progress.isCanceled()) throw new Error("Home Key: Abgebrochen");
        var old = [target.enabled.value, target.issuer.value, target.endpoint.value, target.action.value, target.description.value];
        try {
            target.enabled.value = 0; target.issuer.value = ""; target.endpoint.value = "";
            target.action.value = 0; target.description.value = "";
        } catch (error) {
            try { target.enabled.value = old[0]; target.issuer.value = old[1]; target.endpoint.value = old[2];
                target.action.value = old[3]; target.description.value = old[4]; } catch (ignored) { }
            throw new Error("Home Key: ETS-Platz konnte nicht geleert werden: " + error.message);
        }
        var text = "ETS-Platz " + row + " geleert. Kein Gerät im Reader gelöscht oder gesperrt (Geräte-ID unvollständig). Parameter herunterladen, um die Berechtigung im Gerät zu entfernen. Aktionen bleiben bestehen.";
        HK_setOnlineStatus(device, text); progress.setText(text); progress.setProgress(100);
        return;
    }
    HK_run(device, online, progress, function() {
        var matches = [];
        for (var i = 1; i <= 16; i++) {
            var fields = HK_authorizationFields(device, i);
            if (String(fields.issuer.value).toLowerCase() == issuer && String(fields.endpoint.value).toLowerCase() == endpoint)
                matches.push(fields);
        }
        var status = HK_status(device, online, progress);
        var identity = [];
        var hex = issuer + endpoint;
        for (var n = 0; n < hex.length; n += 2) identity.push(parseInt(hex.substr(n, 2), 16));
        if (progress.isCanceled()) throw new Error("Home Key: Abgebrochen");
        HK_requireOk(HK_call(device, online, progress, [8].concat(status.slice(8, 12), identity)));
        // Clear ETS only after the persistent revocation was acknowledged.
        var snapshots = [];
        for (var i = 0; i < matches.length; i++) {
            var f = matches[i];
            snapshots.push([f.enabled.value, f.issuer.value, f.endpoint.value, f.action.value, f.description.value]);
        }
        try {
            for (var i = 0; i < matches.length; i++) {
                var f = matches[i];
                if (String(f.issuer.value).toLowerCase() != issuer || String(f.endpoint.value).toLowerCase() != endpoint)
                    throw new Error("ETS-Zeile wurde während des Löschens geändert");
                f.enabled.value = 0; f.issuer.value = ""; f.endpoint.value = ""; f.action.value = 0; f.description.value = "";
            }
        } catch (error) {
            for (var i = 0; i < matches.length; i++) {
                var f = matches[i], old = snapshots[i];
                try { f.enabled.value = old[0]; f.issuer.value = old[1]; f.endpoint.value = old[2]; f.action.value = old[3]; f.description.value = old[4]; } catch (ignored) { }
            }
            throw new Error("Gerät im Reader entfernt; ETS-Zeilen nicht geleert. Erneut versuchen: " + error.message);
        }
        var text = "Gerät gelöscht und gesperrt; " + matches.length + " ETS-Berechtigung(en) geleert. Wallet-Eintrag und Aktionszuordnungen bleiben bestehen. ETS-Projekt speichern und Parameter herunterladen.";
        HK_setOnlineStatus(device, text); progress.setText(text);
    });
}
function HK_deleteAuthorization01(device, online, progress, context) { HK_deleteAuthorization(1, device, online, progress, context); }
function HK_deleteAuthorization02(device, online, progress, context) { HK_deleteAuthorization(2, device, online, progress, context); }
function HK_deleteAuthorization03(device, online, progress, context) { HK_deleteAuthorization(3, device, online, progress, context); }
function HK_deleteAuthorization04(device, online, progress, context) { HK_deleteAuthorization(4, device, online, progress, context); }
function HK_deleteAuthorization05(device, online, progress, context) { HK_deleteAuthorization(5, device, online, progress, context); }
function HK_deleteAuthorization06(device, online, progress, context) { HK_deleteAuthorization(6, device, online, progress, context); }
function HK_deleteAuthorization07(device, online, progress, context) { HK_deleteAuthorization(7, device, online, progress, context); }
function HK_deleteAuthorization08(device, online, progress, context) { HK_deleteAuthorization(8, device, online, progress, context); }
function HK_deleteAuthorization09(device, online, progress, context) { HK_deleteAuthorization(9, device, online, progress, context); }
function HK_deleteAuthorization10(device, online, progress, context) { HK_deleteAuthorization(10, device, online, progress, context); }
function HK_deleteAuthorization11(device, online, progress, context) { HK_deleteAuthorization(11, device, online, progress, context); }
function HK_deleteAuthorization12(device, online, progress, context) { HK_deleteAuthorization(12, device, online, progress, context); }
function HK_deleteAuthorization13(device, online, progress, context) { HK_deleteAuthorization(13, device, online, progress, context); }
function HK_deleteAuthorization14(device, online, progress, context) { HK_deleteAuthorization(14, device, online, progress, context); }
function HK_deleteAuthorization15(device, online, progress, context) { HK_deleteAuthorization(15, device, online, progress, context); }
function HK_deleteAuthorization16(device, online, progress, context) { HK_deleteAuthorization(16, device, online, progress, context); }


// ETS validates edits before download; the firmware independently rejects the
// entire table on duplicate IDs or identities. Disabled draft rows are ignored.
function HK_validateAssignments(input, changed, prevValue, context) {
    if (Number(input.HomeKeyEnabled) != 1 || Number(input.EtsAuthorization) != 1) return true;
    var ids = {}, identities = {};
    for (var row = 1; row <= 16; row++) {
        var prefix = "Row" + (row < 10 ? "0" : "") + row;
        if (Number(input[prefix + "Enabled"]) != 1) continue;
        var id = Number(input[prefix + "AccessId"]);
        if (id < 0 || id > 1499 || id % 1 != 0)
            return "Home Key: Berechtigung " + row + " benötigt eine NFC-Tag-ID von 0 bis 1499.";
        if (ids[id]) return "Home Key: NFC-Tag-ID " + id + " ist in Berechtigung " + ids[id] + " und " + row + " doppelt. Eigene IDs vergeben; doppelte IDs sperren die gesamte Home-Key-Tabelle.";
        ids[id] = row;
        var issuer = String(input[prefix + "Issuer"]).toLowerCase();
        var endpoint = String(input[prefix + "Endpoint"]).toLowerCase();
        // Empty identities must remain editable when a new row is enabled.
        if (/^[0-9a-f]{16}$/.test(issuer) && /^[0-9a-f]{12}$/.test(endpoint)) {
            var key = issuer + ":" + endpoint;
            if (identities[key]) return "Home Key: Dasselbe Gerät steht in Berechtigung " + identities[key] + " und " + row + ". Nur eine Zeile aktivieren.";
            identities[key] = row;
        }
    }
    return true;
}

// Optional extension used by AccessControl's Info holen. The connection belongs
// to the caller. Names/assignments are from the current ETS project, not proof
// that this project's parameters have already been downloaded to the reader.
function HK_nfcAssignmentInfo(device, online, progress, id, uidFound) {
    var rows = [], validation = {};
    var enabled = device.getParameterByName("HK_Enabled");
    var ets = device.getParameterByName("HK_EtsAuthorization");
    if (!enabled || !ets) return null;
    validation.HomeKeyEnabled = enabled.value;
    validation.EtsAuthorization = ets.value;
    for (var row = 1; row <= 16; row++) {
        var fields = HK_authorizationFields(device, row);
        var prefix = "Row" + (row < 10 ? "0" : "") + row;
        validation[prefix + "Enabled"] = fields.enabled.value;
        validation[prefix + "Issuer"] = fields.issuer.value;
        validation[prefix + "Endpoint"] = fields.endpoint.value;
        validation[prefix + "AccessId"] = fields.action.value;
        if (Number(fields.enabled.value) == 1 && Number(fields.action.value) == Number(id))
            rows.push({row: row, fields: fields});
    }
    if (!rows.length) return null;
    var names = [];
    for (var i = 0; i < rows.length; i++)
        names.push(String(rows[i].fields.description.value) || "Berechtigung " + rows[i].row);
    var label = "ETS: Home Key " + names.join(", ");
    if (Number(enabled.value) != 1) return "Home Key deaktiviert; " + label;
    if (Number(ets.value) != 1) return "USB-Berechtigungen aktiv; ETS-Zuordnung nicht aktiv: " + label;
    if (uidFound) return "KONFLIKT: UID-Tag und Home Key nutzen ID " + id + "; " + label;
    var valid = HK_validateAssignments(validation);
    if (valid !== true) return valid + " " + label;
    var target = rows[0].fields;
    var issuer = String(target.issuer.value).toLowerCase(), endpoint = String(target.endpoint.value).toLowerCase();
    if (!/^[0-9a-f]{16}$/.test(issuer) || !/^[0-9a-f]{12}$/.test(endpoint))
        return "Ungültige Geräte-ID; " + label;
    var entries = HK_readEndpoints(device, online, progress), found = false;
    for (var i = 0; i < entries.length; i++)
        if (entries[i].issuer == issuer && entries[i].endpoint == endpoint) found = true;
    return (found ? "Provisioniert; " : "Nicht provisioniert; ") + label + "; Zuordnung nach ETS-Download wirksam";
}
