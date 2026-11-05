### Konventionelle UID-Tags zulassen

Ohne Apple Home Key ist die UID-Auswertung bei ausgewähltem NFC-Scanner immer aktiv. Ein früher gespeicherter Nein-Wert dieses Parameters blockiert die normale NFC-Verwendung nicht. Auch ISO-DEP-Karten werden in diesem Modus ausschließlich über ihre UID ausgewertet, ohne HomeKey-SELECT oder Authentifizierung.

Bei aktiviertem Apple Home Key kann die UID-Verwendung zusätzlich gesteuert werden:

* **Ja** (Standard): Bekannte UID-Tags werden zusätzlich über die bestehenden NFC-Zuordnungen ausgewertet.
* **Nein**: Nur HomeKey-Authentifizierung; herkömmliche UID-Tags werden nicht für Zutritt oder Anlernen verwendet.

Die Freigabe erlaubt keine unbekannten Tags: Die UID muss gespeichert sein und eine passende NFC-Aktion besitzen. Eine UID ist kein kryptografischer Identitätsnachweis. Ein fehlgeschlagener HomeKey-Authentifizierungsversuch wird im HomeKey-Modus niemals als UID-Zutritt behandelt. Auch ein Speicher- oder Initialisierungsfehler hebt die konfigurierte UID-Sperre nicht auf.

NFC-Tag-IDs, die für HomeKey-Berechtigungen verwendet werden, stehen nicht gleichzeitig für UID-Tags zur Verfügung.
