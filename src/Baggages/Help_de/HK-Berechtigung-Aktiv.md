### Berechtigung aktiv

Mit diesem Parameter wird die HomeKey-Berechtigung des ausgewählten Platzes aktiviert.

* **Nein** (Standard): Der Eintrag wird für den Zutritt nicht ausgewertet. Eingelesene Geräte sind zunächst deaktiviert.
* **Ja**: Das durch Issuer-ID und Endpoint-ID bezeichnete Gerät darf nach erfolgreicher Authentifizierung die Aktionen seiner NFC-Tag-ID auslösen.

Die Änderung wird erst mit dem Parameterdownload wirksam. Für eine Aktion gelten weiterhin deren Sperren und Authentifizierungsbedingungen. Prüfen Sie die NFC-Tag-ID und die Aktionszuordnungen, bevor Sie einen Eintrag aktivieren.

Das Deaktivieren entfernt weder die Geräteschlüssel noch den Eintrag in Apple Wallet. Verwenden Sie **Vorgang → löschen**, wenn das Gerät dauerhaft gesperrt und der Platz freigegeben werden soll.
