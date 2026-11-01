#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32) && defined(OPENKNX_HOMEKEY_WEB)
#include "HomeKeyModule.h"
#include "HomeKeyProvisioning.h"
#include "HomeKeyCodeRequest.h"
#include "NetworkModule.h"
#include "webassets.h"
#include <esp_random.h>
#include <sodium.h>

using namespace OpenKNX::Network;

void HomeKeyModule::setupWeb()
{
    webCommands = xQueueCreate(1, sizeof(WebCodeCommand));
    if (!webCommands) { logErrorP("Home Key web command queue unavailable"); return; }
    uint8_t random[16];
    esp_fill_random(random, sizeof(random));
    constexpr char digits[] = "0123456789abcdef";
    for (auto byte : random) { webToken += digits[byte >> 4]; webToken += digits[byte & 15]; }

    auto& web = openknxNetwork.webserver;
    web.addMenuItem("Home Key", "/homekey", 20);
    web.addRoute(WEB_GET, "/assets/homekey.js",
        Webserver::Asset(WebAssets::homekey_js_mime, WebAssets::homekey_js_gz, sizeof(WebAssets::homekey_js_gz)));
    web.addRoute(WEB_GET, "/assets/homekey.css",
        Webserver::Asset(WebAssets::homekey_css_mime, WebAssets::homekey_css_gz, sizeof(WebAssets::homekey_css_gz)));
    web.addRoute(WEB_GET, "/assets/qrcode.js",
        Webserver::Asset(WebAssets::qrcode_js_mime, WebAssets::qrcode_js_gz, sizeof(WebAssets::qrcode_js_gz)));
    web.addRoute(WEB_GET, "/homekey", [this](WebRequest&, WebResponse& response)
    {
        response.setHeader("Cache-Control", "no-store");
        response.setHeader("Referrer-Policy", "same-origin");
        response.setLayout(true);
        response.setActiveMenu("/homekey");
        std::string html = "<link rel='stylesheet' href='/assets/homekey.css'><div class='container homekey'>";
        html += "<h1>Home Key</h1><p>Apple Home Key wird unter Zutrittskontrolle in der ETS eingerichtet.</p>";
        html += "<section><h2>Setup-Code</h2><p>Zum Speichern m&uuml;ssen Apple Home Key und die NFC-Inbetriebnahme "
                "in der ETS freigegeben und der Programmiermodus aktiv sein.</p>"
                "<p>Nur in einem vertrauensw&uuml;rdigen Netzwerk verwenden: HTTP ist nicht verschl&uuml;sselt. "
                "Der gespeicherte Code wird nicht angezeigt. Das Speichern &ouml;ffnet kein Pairing-Fenster.</p>"
                "<p>Entwicklungsversion: Die Webkonsole hat keine Anmeldung. "
                "Geben Sie in der Webkonsole keine Passw&ouml;rter oder Setup-Codes ein; "
                "eingegebene Befehle werden anderen verbundenen Konsolen angezeigt.</p>";
        html += "<form id='homekey-form' data-token='" + webToken + "'>"
                "<div id='homekey-previous-row' hidden><label for='homekey-previous'>Bisheriger Setup-Code (8 Ziffern)</label>"
                "<input id='homekey-previous' type='password' inputmode='numeric' pattern='[0-9]{8}' "
                "minlength='8' maxlength='8' autocomplete='current-password'></div>"
                "<label for='homekey-code'>Neuer Setup-Code (8 Ziffern)</label>"
                "<input id='homekey-code' type='password' inputmode='numeric' pattern='[0-9]{8}' minlength='8' "
                "maxlength='8' autocomplete='new-password' required>"
                "<label for='homekey-confirm'>Setup-Code wiederholen</label>"
                "<input id='homekey-confirm' type='password' inputmode='numeric' pattern='[0-9]{8}' minlength='8' "
                "maxlength='8' autocomplete='new-password' required>"
                "<button id='homekey-save' type='submit' disabled>Setup-Code speichern</button></form>"
                "<p id='homekey-permission'></p><p id='homekey-result' role='status' aria-live='polite'></p>"
                "</section><section><h2>In Apple Home hinzuf&uuml;gen</h2>"
                "<p>&Ouml;ffnen Sie das Pairing-Fenster in der ETS. Geben Sie den aktuellen Setup-Code ein "
                "und scannen Sie den QR-Code mit der iPhone-Kamera oder in Apple Home unter "
                "&bdquo;Ger&auml;t hinzuf&uuml;gen&ldquo;. iPhone und Ger&auml;t m&uuml;ssen im selben Netzwerk sein.</p>"
                "<p>Der QR-Code wird nur in diesem Browser erzeugt und enth&auml;lt den eingegebenen Setup-Code. "
                "Nicht weitergeben. Ein falscher Code verhindert die Kopplung; die Eingabe wird hier nicht gepr&uuml;ft.</p>"
                "<form id='homekey-qr-form'><label for='homekey-qr-code'>Aktueller Setup-Code (8 Ziffern)</label>"
                "<input id='homekey-qr-code' type='password' inputmode='numeric' pattern='[0-9]{8}' "
                "minlength='8' maxlength='8' autocomplete='off' required>"
                "<button id='homekey-qr-show' type='submit' disabled>QR-Code anzeigen</button></form>"
                "<p id='homekey-qr-result' role='status' aria-live='polite'></p>"
                "<div id='homekey-qr' role='img' aria-label='Apple Home Kopplungs-QR-Code' hidden></div>"
                "<button id='homekey-qr-hide' type='button' hidden>QR-Code ausblenden</button>"
                "</section><section><h2>Status</h2><table class='attribute-table'><tbody>"
                "<tr><td>Apple Home Key</td><td id='homekey-enabled'>Wird abgefragt</td></tr>"
                "<tr><td>NFC-Inbetriebnahme</td><td id='homekey-commissioning'>Wird abgefragt</td></tr>"
                "<tr><td>Pairing-Fenster</td><td id='homekey-pairing'>Wird abgefragt</td></tr>"
                "</tbody></table><p>Das Pairing-Fenster wird in der ETS oder an der lokalen USB-Konsole ge&ouml;ffnet. "
                "Die Kopplung in Apple Home allein gew&auml;hrt keinen Zutritt.</p></section></div>"
                "<script src='/assets/qrcode.js' defer></script><script src='/assets/homekey.js' defer></script>";
        response.send(html.c_str());
    });
    web.addRoute(WEB_GET, "/homekey/status", [this](WebRequest&, WebResponse& response)
    {
        response.setHeader("Cache-Control", "no-store");
        response.setContentType("application/json");
        std::string json = "{\"enabled\":";
        json += enabled ? "true" : "false";
        json += ",\"commissioning\":";
        json += pairingPermitted ? "true" : "false";
        json += ",\"pairing\":";
        json += pairingAllowed() ? "true" : "false";
        json += ",\"canSetCode\":";
        json += webCodeAllowed ? "true" : "false";
        json += ",\"codeConfigured\":";
        json += codeConfigured ? "true" : "false";
        json += ",\"setupId\":\"";
        if (provisioningReady) json += qrSetupId.data();
        json += "\"";
        const uint32_t now = millis(), deadline = codeBlockedUntil.load();
        json += ",\"retryAfter\":" + std::to_string(HomeKey::windowOpen(now, deadline) ? (deadline - now + 999) / 1000 : 0);
        json += ",\"codeState\":" + std::to_string(static_cast<unsigned>(webCodeState.load())) + "}";
        response.send(json.c_str());
    });
    web.addRoute(WEB_POST, "/homekey/code", [this](WebRequest& request, WebResponse& response)
    {
        response.setHeader("Cache-Control", "no-store");
        response.setContentType("application/json");
        HomeKey::CodeRequest parsed;
        if (!HomeKey::parseCodeRequest(request.body(), request.bodyLength(), parsed))
        {
            logWarningP("Invalid web setup-code request");
            response.setStatus(400); response.send("{\"error\":\"Ungültige Anfrage; Setup-Codes müssen acht Ziffern enthalten.\"}"); return;
        }
        const bool tokenValid = webToken.size() == 32 && sodium_memcmp(parsed.token, webToken.data(), 32) == 0;
        if (!tokenValid)
        {
            sodium_memzero(&parsed, sizeof(parsed));
            logWarningP("Web setup-code request token rejected");
            response.setStatus(403); response.send("{\"error\":\"Seite neu laden; Anfrage nicht freigegeben.\"}"); return;
        }
        if (!webCodeAllowed)
        {
            sodium_memzero(&parsed, sizeof(parsed));
            logWarningP("Web setup-code change not permitted");
            response.setStatus(403); response.send("{\"error\":\"ETS-Freigaben und Programmiermodus erforderlich.\"}"); return;
        }
        const uint32_t now = millis(), deadline = codeBlockedUntil.load();
        if (HomeKey::windowOpen(now, deadline))
        {
            sodium_memzero(&parsed, sizeof(parsed));
            response.setHeader("Retry-After", std::to_string((deadline - now + 999) / 1000).c_str());
            response.setStatus(429); response.send("{\"error\":\"Zu viele Fehlversuche; fünf Minuten warten.\"}"); return;
        }
        auto previous = webCodeState.load();
        if (previous == WebCodeState::Pending || !webCodeState.compare_exchange_strong(previous, WebCodeState::Pending))
        {
            sodium_memzero(&parsed, sizeof(parsed));
            response.setStatus(409); response.send("{\"error\":\"Speichern läuft bereits; bitte warten.\"}"); return;
        }
        WebCodeCommand command;
        command.epoch = commissioningEpoch.load();
        std::copy(std::begin(parsed.code), std::end(parsed.code), command.code);
        std::copy(std::begin(parsed.previous), std::end(parsed.previous), command.previous);
        sodium_memzero(&parsed, sizeof(parsed));
        const bool queued = xQueueSend(webCommands, &command, 0) == pdTRUE;
        sodium_memzero(&command, sizeof(command));
        if (!queued)
        {
            webCodeState = WebCodeState::Failed;
            logErrorP("Web setup-code command queue full");
            response.setStatus(503); response.send("{\"error\":\"Speichern derzeit nicht möglich; erneut versuchen.\"}"); return;
        }
        response.setStatus(202);
        response.send("{\"queued\":true}");
    });
}

void HomeKeyModule::loopWeb()
{
    webCodeAllowed = enabled && pairingPermitted && provisioningReady && knx.progMode();
    WebCodeCommand command;
    if (!webCommands || xQueueReceive(webCommands, &command, 0) != pdTRUE) return;
    if (!webCodeAllowed || command.epoch != commissioningEpoch.load())
    {
        webCodeState = WebCodeState::Rejected;
        logWarningP("Web setup-code change no longer permitted");
    }
    else if (!provisioning->configurePairingCode(command.code, command.previous, true))
    {
        webCodeState = WebCodeState::Failed;
        logWarningP("Web setup-code change could not be queued");
    }
    sodium_memzero(&command, sizeof(command));
}
#endif
