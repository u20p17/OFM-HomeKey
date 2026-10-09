// ETS online button handlers. Only public IDs cross KNX; setup codes stay local.
var HK_onlineErrors = ["", "Gerät beschäftigt", "Ungültige Anfrage", "ETS-Anwendung zuerst herunterladen", "Home Key nicht gestartet", "Inbetriebnahme gesperrt", "Warteschlange beschäftigt", "Speicher/Krypto nicht verfügbar", "Geräteliste geändert; erneut lesen", "Endpoint nicht gefunden"];
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
            var entry = { issuer: HK_hex(response, 8, 8), endpoint: HK_hex(response, 16, 6) };
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
                endpoint: device.getParameterByName(prefix + "Endpoint"), action: device.getParameterByName(prefix + "AccessId") };
            if (!fields.enabled || !fields.issuer || !fields.endpoint || !fields.action) throw new Error("Home Key: ETS-Tabelle fehlt; passende Anwendung verwenden");
            var issuer = String(fields.issuer.value).toLowerCase(), endpoint = String(fields.endpoint.value).toLowerCase();
            if (issuer || endpoint || Number(fields.enabled.value) != 0 || Number(fields.action.value) != 0) {
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
        var text = plan.length + " Geräte-IDs importiert (deaktiviert). NFC-Aktions-IDs vergeben, Zeilen aktivieren und herunterladen.";
        HK_setOnlineStatus(device, text); progress.setText(text);
    });
}
