### Konventionelle UID-Tags zulassen

Hier wird festgelegt, ob bei aktiviertem HomeKey zusätzlich herkömmliche NFC-Tags ausgewertet werden.

* **Ja** (Standard): Gespeicherte UID-Tags können Zutrittsaktionen auslösen und angelernt werden.
* **Nein**: Nur erfolgreich authentifizierte HomeKey-Geräte werden ausgewertet. UID-Tags werden weder für den Zutritt noch zum Anlernen verwendet.

Die Freigabe berechtigt keine unbekannten Tags. Für den Zutritt muss die UID gespeichert und die NFC-Tag-ID einer Aktion zugeordnet sein. Eine UID ist kein kryptografischer Identitätsnachweis.

Ohne aktiviertes HomeKey ist die normale UID-Auswertung unabhängig von diesem Parameter eingeschaltet. Ein fehlgeschlagener HomeKey-Authentifizierungsversuch wird bei aktiviertem HomeKey nicht als UID-Zutritt behandelt.

UID-Tags und HomeKey-Geräte teilen sich den ID-Bereich 0–1499. Eine belegte NFC-Tag-ID darf nicht gleichzeitig einem anderen Zugang zugewiesen werden.
