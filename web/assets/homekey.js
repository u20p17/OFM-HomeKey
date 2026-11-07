(() => {
    const form = document.getElementById('homekey-form');
    if (!form) return;
    const code = document.getElementById('homekey-code');
    const previous = document.getElementById('homekey-previous');
    const previousRow = document.getElementById('homekey-previous-row');
    const confirmation = document.getElementById('homekey-confirm');
    const save = document.getElementById('homekey-save');
    const result = document.getElementById('homekey-result');
    const permission = document.getElementById('homekey-permission');
    const qrForm = document.getElementById('homekey-qr-form');
    const qrCode = document.getElementById('homekey-qr-code');
    const qrShow = document.getElementById('homekey-qr-show');
    const qrHide = document.getElementById('homekey-qr-hide');
    const qrImage = document.getElementById('homekey-qr');
    const qrResult = document.getElementById('homekey-qr-result');
    let setupId = '';
    let submitting = false;
    let waiting = false;
    let lastState = null;

    function message(text, error = false) {
        result.textContent = text;
        result.dataset.error = String(error);
    }

    const tabs = ['setup', 'pairing'];
    const tabHashes = { setup: '#setup-code', pairing: '#apple-home' };
    function tabFromHash() {
        const hash = window.location ? window.location.hash : '';
        if (hash === '#status' && window.history) window.history.replaceState(null, '', tabHashes.setup);
        return tabs.find(tab => tabHashes[tab] === hash) || 'setup';
    }
    function selectTab(name, updateHash = true) {
        for (const tab of tabs) {
            document.getElementById('homekey-panel-' + tab).hidden = tab !== name;
            const button = document.getElementById('homekey-tab-' + tab);
            button.setAttribute('aria-selected', String(tab === name));
            button.tabIndex = tab === name ? 0 : -1;
        }
        if (name !== 'pairing') clearQr();
        if (updateHash && window.history) {
            window.history.replaceState(null, '', tabHashes[name]);
        }
    }
    tabs.forEach((tab, index) => {
        const button = document.getElementById('homekey-tab-' + tab);
        button.addEventListener('click', () => selectTab(tab));
        button.addEventListener('keydown', event => {
            if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
            event.preventDefault();
            const next = event.key === 'Home' ? 0 : event.key === 'End' ? tabs.length - 1 :
                (index + (event.key === 'ArrowRight' ? 1 : -1) + tabs.length) % tabs.length;
            selectTab(tabs[next]);
            document.getElementById('homekey-tab-' + tabs[next]).focus();
        });
    });
    window.addEventListener('hashchange', () => selectTab(tabFromHash(), false));
    selectTab(tabFromHash(), false);

    function validCode(value) {
        return /^\d{8}$/.test(value) && !/^(\d)\1{7}$/.test(value) &&
            value !== '12345678' && value !== '87654321';
    }

    function hideQr() {
        qrImage.replaceChildren();
        qrImage.hidden = qrHide.hidden = true;
        document.getElementById('homekey-qr-placeholder').hidden = false;
    }
    function clearQr() {
        hideQr();
        qrCode.value = '';
    }

    qrHide.addEventListener('click', clearQr);
    qrCode.addEventListener('input', () => {
        qrImage.replaceChildren();
        qrImage.hidden = qrHide.hidden = true;
        document.getElementById('homekey-qr-placeholder').hidden = false;
    });
    window.addEventListener('pagehide', clearQr);
    qrForm.addEventListener('submit', event => {
        event.preventDefault();
        if (qrShow.disabled) return;
        if (!validCode(qrCode.value)) {
            hideQr();
            qrResult.textContent = 'Bitte einen gültigen Setup-Code mit acht Ziffern eingeben.';
            return;
        }
        try {
            // HomeSpan HapQR: category Locks (6), IP pairing flag, 9 base-36 digits.
            const bits = 6 * 2 ** 31 + 2 * 2 ** 27 + Number(qrCode.value);
            const payload = `X-HM://${bits.toString(36).toUpperCase().padStart(9, '0')}${setupId}`;
            const qr = qrcode(0, 'M');
            qr.addData(payload, 'Alphanumeric');
            qr.make();
            qrImage.innerHTML = qr.createSvgTag({ cellSize: 8, margin: 32, scalable: true });
            qrImage.hidden = qrHide.hidden = false;
            document.getElementById('homekey-qr-placeholder').hidden = true;
            qrResult.textContent = 'Jetzt mit dem iPhone scannen. Die Kopplung allein gewährt keinen Zutritt.';
        } catch (error) {
            clearQr();
            qrResult.textContent = `QR-Code konnte nicht erzeugt werden: ${error.message}`;
        } finally {
            qrCode.value = '';
        }
    });

    async function refresh() {
        try {
            const response = await fetch('/homekey/status', { cache: 'no-store' });
            if (!response.ok) throw new Error('Status konnte nicht abgefragt werden.');
            const status = await response.json();
            if (status.configured === false) {
                setupId = '';
                save.disabled = qrShow.disabled = true;
                hideQr();
                const version = /^\d{1,2}\.\d{1,2}$/.test(status.applicationVersion) ? ` ${status.applicationVersion}` : '';
                const instruction = `ETS-Anwendung${version} vollständig programmieren. Die Anwendung im Gerät fehlt oder passt nicht zur Firmware; der Programmiermodus allein reicht nicht aus.`;
                qrResult.dataset.state = 'blocked';
                qrResult.textContent = permission.textContent = instruction;
                permission.dataset.state = 'waiting';
                document.getElementById('homekey-connection').dataset.state = 'waiting';
                document.getElementById('homekey-summary').textContent = instruction;
                for (const id of ['homekey-enabled', 'homekey-commissioning', 'homekey-pairing']) {
                    const badge = document.getElementById(id);
                    badge.textContent = 'Nicht verfügbar';
                    badge.dataset.state = 'unknown';
                }
                waiting = false;
                lastState = null;
                message('');
                return;
            }
            setupId = /^[0-9A-Z]{4}$/.test(status.setupId) ? status.setupId : '';
            qrShow.disabled = !status.enabled || !status.commissioning || !status.pairing ||
                !status.codeConfigured || !setupId || status.codeState === 1 || submitting || waiting;
            if (qrShow.disabled) {
                hideQr();
                qrResult.dataset.state = 'blocked';
                qrResult.textContent = !status.enabled
                    ? 'In der ETS „Apple Home Key aktivieren“ auf Ja setzen und die Parameter laden.'
                    : !status.commissioning
                    ? 'In der ETS „NFC-Inbetriebnahme über USB/KNX erlauben“ auf Ja setzen und die Parameter laden. Der eingegebene Code bleibt erhalten.'
                    : !status.codeConfigured ? 'Zuerst im Tab Setup-Code einen Code speichern. Der hier eingegebene Code bleibt erhalten.'
                    : !status.pairing ? 'In der ETS „Pairing öffnen (5 Minuten)“ wählen. Der eingegebene Code bleibt erhalten.'
                    : 'Initialisierung oder Speicherung läuft. Bitte kurz warten; der eingegebene Code bleibt erhalten.';
            } else if (qrResult.dataset.state === 'blocked') {
                qrResult.dataset.state = 'ready';
                qrResult.textContent = 'Pairing-Fenster geöffnet. Der eingegebene Code kann jetzt verwendet werden.';
            }
            previousRow.hidden = !status.codeConfigured;
            previous.required = status.codeConfigured;
            for (const [id, active, yes, no] of [
                ['homekey-enabled', status.enabled, 'Aktiviert', 'Deaktiviert'],
                ['homekey-commissioning', status.commissioning, 'Erlaubt', 'Gesperrt'],
                ['homekey-pairing', status.pairing, 'Geöffnet', 'Geschlossen']
            ]) {
                const badge = document.getElementById(id);
                badge.textContent = active ? yes : no;
                badge.dataset.state = active ? 'on' : 'off';
            }
            document.getElementById('homekey-connection').dataset.state = 'ready';
            document.getElementById('homekey-summary').textContent = !status.enabled
                ? 'Apple Home Key ist in der ETS deaktiviert.'
                : !status.commissioning ? 'Home Key aktiv · NFC-Inbetriebnahme in der ETS gesperrt'
                : status.pairing ? 'Home Key aktiv · Pairing-Fenster geöffnet'
                : status.codeConfigured ? 'Home Key aktiv · Setup-Code gespeichert'
                : 'Home Key aktiv · Noch kein Setup-Code gespeichert';
            permission.dataset.state = status.canSetCode && !status.retryAfter ? 'ready' : 'waiting';
            permission.textContent = status.retryAfter
                ? `Zu viele Fehlversuche. In ${status.retryAfter} Sekunden erneut versuchen.`
                : !status.enabled ? 'In der ETS „Apple Home Key aktivieren“ auf Ja setzen und die Parameter laden.'
                : !status.commissioning ? 'In der ETS „NFC-Inbetriebnahme über USB/KNX erlauben“ auf Ja setzen und die Parameter laden.'
                : status.canSetCode
                ? status.codeConfigured ? 'Freigabe aktiv. Zum Ändern den bisherigen Setup-Code eingeben.' : 'Freigabe aktiv. Ersten Setup-Code festlegen.'
                : 'Programmiermodus am Gerät einschalten. Falls er bereits aktiv ist, die Initialisierung abwarten.';
            save.disabled = !status.canSetCode || status.retryAfter > 0 || submitting || waiting || status.codeState === 1;
            if (status.codeState !== lastState || waiting) {
                if (status.codeState === 1) message('Setup-Code wird gespeichert.');
                if (status.codeState === 2) { waiting = false; message('Setup-Code wurde gespeichert.'); }
                if (status.codeState === 3) { waiting = false; message('Speichern abgebrochen: Freigabe nicht mehr aktiv.', true); }
                if (status.codeState === 4) { waiting = false; message('Setup-Code konnte nicht gespeichert werden. Erneut versuchen.', true); }
                if (status.codeState === 5) { waiting = false; message('Bisheriger Setup-Code ist falsch. Es wurde nichts geändert.', true); }
                if (status.codeState === 6) { waiting = false; message('Zu viele Fehlversuche. Fünf Minuten warten.', true); }
                lastState = status.codeState;
                save.disabled = !status.canSetCode || status.retryAfter > 0 || submitting || waiting || status.codeState === 1;
            }
        } catch (error) {
            qrShow.disabled = true;
            hideQr();
            qrResult.dataset.state = 'blocked';
            qrResult.textContent = 'Keine Verbindung zum Gerät; QR-Code ausgeblendet. Der eingegebene Code bleibt erhalten.';
            save.disabled = true;
            permission.textContent = 'Keine Verbindung zum Gerät.';
            permission.dataset.state = 'waiting';
            for (const id of ['homekey-enabled', 'homekey-commissioning', 'homekey-pairing']) {
                const badge = document.getElementById(id);
                badge.textContent = 'Unbekannt';
                badge.dataset.state = 'unknown';
            }
            document.getElementById('homekey-connection').dataset.state = 'offline';
            document.getElementById('homekey-summary').textContent = 'Keine Verbindung zum Gerät. Verbindung wird erneut versucht.';
            message(error.message, true);
        }
    }

    form.addEventListener('submit', async event => {
        event.preventDefault();
        if (submitting || waiting || save.disabled) return;
        if (previous.required && !/^\d{8}$/.test(previous.value)) {
            message('Bitte den bisherigen Setup-Code mit acht Ziffern eingeben.', true);
            return;
        }
        if (code.value !== confirmation.value) { message('Die Setup-Codes stimmen nicht überein.', true); return; }
        if (!validCode(code.value)) {
            message('Bitte acht Ziffern verwenden; gleiche Ziffern und einfache Folgen sind nicht erlaubt.', true);
            return;
        }
        submitting = true;
        clearQr();
        qrShow.disabled = true;
        save.disabled = true;
        try {
            const response = await fetch('/homekey/code', {
                method: 'POST',
                headers: { 'Content-Type': 'text/plain' },
                body: `${form.dataset.token}\n${previous.required ? previous.value : ''}\n${code.value}`
            });
            previous.value = code.value = confirmation.value = '';
            const body = await response.json();
            if (!response.ok) throw new Error(body.error || 'Setup-Code konnte nicht gespeichert werden.');
            waiting = true;
            message('Speicherauftrag angenommen. Bestätigung wird abgefragt.');
        } catch (error) {
            previous.value = code.value = confirmation.value = '';
            message(error.message, true);
        } finally {
            submitting = false;
        }
        await refresh();
    });

    async function poll() {
        if (!submitting) await refresh();
        window.setTimeout(poll, 2000);
    }
    poll();
})();
