#if defined(OPENKNX_HOMEKEY_ENABLED) && defined(ARDUINO_ARCH_ESP32) && defined(OPENKNX_HOMEKEY_WEB)
#include "HomeKeyModule.h"
#include "HomeKeyProvisioning.h"
#include "HomeKeyCodeRequest.h"
#include "NetworkModule.h"
#include "webassets.h"
#include "buildtime.h"
#include <esp_random.h>
#include <sodium.h>

using namespace OpenKNX::Network;

void HomeKeyModule::setupWeb()
{
    if (webInitialized) return;
    webInitialized = true;
    webCommands = xQueueCreate(1, sizeof(WebCodeCommand));
    if (!webCommands) logErrorP("Home Key web command queue unavailable");
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
        HtmlWriter html(response);
        const std::string assetVersion = std::to_string(BUILD_TIMESTAMP);
        html += "<link rel=\"stylesheet\" href=\"/assets/homekey.css?v=";
        html += assetVersion;
        html += R"HTML(">
<div class="container homekey" id="homekey-page">
  <div class="homekey-title"><h1>Home Key</h1><p>Apple Home einrichten &amp; Zutritt verwalten</p></div>
  <div class="homekey-toolbar" id="homekey-statusbar" role="status" aria-live="polite" aria-atomic="true">
    <div class="homekey-connection-summary"><span class="homekey-dot" id="homekey-connection" data-state="waiting" aria-hidden="true"></span><span id="homekey-summary">Ger&auml;testatus wird abgefragt&hellip;</span></div>
    <div class="homekey-status-items">
      <span class="homekey-status-item">Home Key <span class="homekey-badge" id="homekey-enabled" data-state="unknown">Wird abgefragt</span></span>
      <span class="homekey-status-item">NFC-Inbetriebnahme <span class="homekey-badge" id="homekey-commissioning" data-state="unknown">Wird abgefragt</span></span>
      <span class="homekey-status-item">Pairing-Fenster <span class="homekey-badge" id="homekey-pairing" data-state="unknown">Wird abgefragt</span></span>
    </div>
  </div>
  <noscript><p class="homekey-notice">Bitte JavaScript aktivieren, um Home Key einzurichten.</p></noscript>
  <div class="homekey-tabs" role="tablist" aria-label="Home Key Bereiche">
    <button type="button" id="homekey-tab-setup" role="tab" aria-selected="true" aria-controls="homekey-panel-setup">Setup-Code</button>
    <button type="button" id="homekey-tab-pairing" role="tab" aria-selected="false" aria-controls="homekey-panel-pairing" tabindex="-1">Apple Home</button>
  </div>
  <section id="homekey-panel-setup" role="tabpanel" aria-labelledby="homekey-tab-setup" tabindex="0">
    <div class="homekey-grid">
      <div class="homekey-card">
        <div class="homekey-card-heading"><h2>Setup-Code festlegen</h2><span class="homekey-badge">1. Einrichtung</span></div>
        <p class="homekey-note">Dieser Code wird f&uuml;r die Kopplung in Apple Home verwendet. Der gespeicherte Code wird nicht angezeigt.</p>
        <p class="homekey-notice" id="homekey-permission" role="status">Freigaben werden gepr&uuml;ft&hellip;</p>
        <form id="homekey-form" data-token=")HTML";
        html += webToken;
        html += R"HTML(">
          <div id="homekey-previous-row" hidden><label for="homekey-previous">Bisheriger Setup-Code</label><input id="homekey-previous" type="password" inputmode="numeric" pattern="[0-9]{8}" minlength="8" maxlength="8" autocomplete="current-password"></div>
          <div class="homekey-field"><label for="homekey-code">Neuer Setup-Code</label><input id="homekey-code" type="password" inputmode="numeric" pattern="[0-9]{8}" minlength="8" maxlength="8" autocomplete="new-password" aria-describedby="homekey-code-help" required></div>
          <div class="homekey-field"><label for="homekey-confirm">Setup-Code wiederholen</label><input id="homekey-confirm" type="password" inputmode="numeric" pattern="[0-9]{8}" minlength="8" maxlength="8" autocomplete="new-password" required></div>
          <p class="homekey-hint" id="homekey-code-help">Acht Ziffern, keine Buchstaben. Gleiche Ziffern und einfache Folgen sind nicht erlaubt.</p>
          <div class="homekey-actions"><button class="homekey-primary" id="homekey-save" type="submit" disabled>Setup-Code speichern</button></div>
        </form>
        <p class="homekey-result" id="homekey-result" role="status" aria-live="polite"></p>
      </div>
      <aside class="homekey-card">
        <div class="homekey-card-heading"><h2>Voraussetzungen</h2></div>
        <ol class="homekey-steps">
          <li><strong>In der ETS freigeben</strong><p>&bdquo;Apple Home Key aktivieren&ldquo; und &bdquo;NFC-Inbetriebnahme &uuml;ber USB/KNX erlauben&ldquo; auf Ja setzen. Anschlie&szlig;end die Parameter laden.</p></li>
          <li><strong>Programmiermodus einschalten</strong><p>Die Programmiertaste am Ger&auml;t dr&uuml;cken. Der Programmiermodus muss beim Speichern aktiv sein.</p></li>
          <li><strong>Code speichern</strong><p>Den neuen achtstelligen Code zweimal eingeben. Danach im Tab Apple Home mit der Kopplung fortfahren.</p></li>
        </ol>
        <p class="homekey-hint">Das Speichern &ouml;ffnet noch kein Pairing-Fenster.</p>
      </aside>
    </div>
    <details class="homekey-help"><summary>Hinweise zum Umgang mit Setup-Codes</summary><p>Nur in einem vertrauensw&uuml;rdigen lokalen Netzwerk verwenden: HTTP ist nicht verschl&uuml;sselt. Die Webkonsole dieser Entwicklungsversion hat keine Anmeldung. Dort keine Passw&ouml;rter oder Setup-Codes eingeben; Befehle werden anderen verbundenen Konsolen angezeigt.</p></details>
  </section>
  <section id="homekey-panel-pairing" role="tabpanel" aria-labelledby="homekey-tab-pairing" tabindex="0" hidden>
    <div class="homekey-grid">
      <div class="homekey-card">
        <div class="homekey-card-heading"><h2>In Apple Home hinzuf&uuml;gen</h2><span class="homekey-badge">2. Kopplung</span></div>
        <ol class="homekey-steps">
          <li><strong>Pairing-Fenster &ouml;ffnen</strong><p>In der ETS &bdquo;Pairing &ouml;ffnen (5 Minuten)&ldquo; w&auml;hlen. Die Programmiertaste allein &ouml;ffnet kein Pairing-Fenster.</p></li>
          <li><strong>Setup-Code eingeben</strong><p>Den zuvor gespeicherten Code verwenden. iPhone und Ger&auml;t m&uuml;ssen im selben Netzwerk sein.</p></li>
        </ol>
        <form id="homekey-qr-form"><div class="homekey-field"><label for="homekey-qr-code">Aktueller Setup-Code (8 Ziffern)</label><input id="homekey-qr-code" type="password" inputmode="numeric" pattern="[0-9]{8}" minlength="8" maxlength="8" autocomplete="off" required></div><div class="homekey-actions"><button class="homekey-primary" id="homekey-qr-show" type="submit" disabled>QR-Code anzeigen</button></div></form>
        <p class="homekey-result" id="homekey-qr-result" role="status" aria-live="polite"></p>
      </div>
      <div class="homekey-card homekey-qr-card">
        <div class="homekey-card-heading"><h2>Kopplungs-QR-Code</h2></div>
        <p class="homekey-note" id="homekey-qr-placeholder">Nach der Freigabe erscheint hier der QR-Code. Mit der iPhone-Kamera oder in Apple Home unter &bdquo;Ger&auml;t hinzuf&uuml;gen&ldquo; scannen.</p>
        <div id="homekey-qr" role="img" aria-label="Apple Home Kopplungs-QR-Code" hidden></div>
        <button id="homekey-qr-hide" type="button" hidden>QR-Code ausblenden</button>
        <p class="homekey-hint">Der QR-Code enth&auml;lt den eingegebenen Setup-Code. Er wird nur in diesem Browser erzeugt und nicht weitergegeben. Ein falscher Code verhindert die Kopplung.</p>
      </div>
    </div>
  </section>
  <details class="homekey-help"><summary>Status und Zutrittsberechtigungen</summary><p>Der Status in der Leiste wird automatisch aktualisiert. Das Pairing-Fenster l&auml;sst sich in der ETS oder an der lokalen USB-Konsole &ouml;ffnen. Die Kopplung in Apple Home allein gew&auml;hrt keinen Zutritt: Provisionierte Ger&auml;te in der ETS importieren, Berechtigung aktivieren und eine freie NFC-Tag-ID vergeben. Diese unter Zutrittskontrolle als Tag der gew&uuml;nschten Aktion zuordnen, deren Schaltobjekt mit dem KNX-Aktor verbinden und die Parameter laden.</p></details>
</div>
)HTML";
        html += "<script src=\"/assets/qrcode.js?v=";
        html += assetVersion;
        html += "\" defer></script><script src=\"/assets/homekey.js?v=";
        html += assetVersion;
        html += "\" defer></script>";
    });
    web.addRoute(WEB_GET, "/homekey/status", [this](WebRequest&, WebResponse& response)
    {
        response.setHeader("Cache-Control", "no-store");
        response.setContentType("application/json");
        const bool configured = knx.configured();
        std::string json = "{\"configured\":";
        json += configured ? "true" : "false";
        json += ",\"applicationVersion\":\"" + std::to_string(MAIN_ApplicationVersion >> 4) + "." + std::to_string(MAIN_ApplicationVersion & 15) + "\"";
        json += ",\"enabled\":";
        json += configured && enabled ? "true" : "false";
        json += ",\"commissioning\":";
        json += configured && pairingPermitted ? "true" : "false";
        json += ",\"pairing\":";
        json += configured && pairingAllowed() ? "true" : "false";
        json += ",\"canSetCode\":";
        json += configured && webCommands && webCodeAllowed ? "true" : "false";
        json += ",\"codeConfigured\":";
        json += codeConfigured ? "true" : "false";
        json += ",\"setupId\":\"";
        if (configured && provisioningReady) json += qrSetupId.data();
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
        if (!knx.configured())
        {
            response.setStatus(409);
            response.send("{\"error\":\"Passende ETS-Anwendung zuerst vollständig programmieren.\"}");
            return;
        }
        if (!webCommands)
        {
            response.setStatus(503);
            response.send("{\"error\":\"Speichern derzeit nicht verfügbar; Gerät neu starten.\"}");
            return;
        }
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
    webCodeAllowed = knx.configured() && webCommands && enabled && pairingPermitted && provisioningReady && knx.progMode();
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
