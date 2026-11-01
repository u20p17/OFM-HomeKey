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

    function validCode(value) {
        return /^\d{8}$/.test(value) && !/^(\d)\1{7}$/.test(value) &&
            value !== '12345678' && value !== '87654321';
    }

    function clearQr() {
        qrImage.replaceChildren();
        qrImage.hidden = qrHide.hidden = true;
        qrCode.value = '';
    }

    qrHide.addEventListener('click', clearQr);
    qrCode.addEventListener('input', () => {
        qrImage.replaceChildren();
        qrImage.hidden = qrHide.hidden = true;
    });
    window.addEventListener('pagehide', clearQr);
    qrForm.addEventListener('submit', event => {
        event.preventDefault();
        if (qrShow.disabled) return;
        if (!validCode(qrCode.value)) {
            clearQr();
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
            setupId = /^[0-9A-Z]{4}$/.test(status.setupId) ? status.setupId : '';
            qrShow.disabled = !status.enabled || !status.commissioning || !status.pairing ||
                !status.codeConfigured || !setupId || status.codeState === 1 || submitting || waiting;
            if (qrShow.disabled) {
                clearQr();
                qrResult.textContent = 'Zum Anzeigen einen Setup-Code festlegen und das Pairing-Fenster öffnen.';
            }
            previousRow.hidden = !status.codeConfigured;
            previous.required = status.codeConfigured;
            document.getElementById('homekey-enabled').textContent = status.enabled ? 'Aktiviert' : 'Deaktiviert';
            document.getElementById('homekey-commissioning').textContent = status.commissioning ? 'Erlaubt' : 'Gesperrt';
            document.getElementById('homekey-pairing').textContent = status.pairing ? 'Geöffnet' : 'Geschlossen';
            permission.textContent = status.retryAfter
                ? `Zu viele Fehlversuche. In ${status.retryAfter} Sekunden erneut versuchen.`
                : status.canSetCode
                ? status.codeConfigured ? 'Zum Ändern den bisherigen Setup-Code eingeben.' : 'Ersten Setup-Code festlegen.'
                : 'Zum Speichern ETS-Freigaben aktivieren und Programmiermodus einschalten.';
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
            clearQr();
            qrResult.textContent = 'Keine Verbindung zum Gerät; QR-Code ausgeblendet.';
            save.disabled = true;
            permission.textContent = 'Keine Verbindung zum Gerät.';
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
