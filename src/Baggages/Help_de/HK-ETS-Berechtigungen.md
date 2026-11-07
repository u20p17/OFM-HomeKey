### Berechtigungen aus ETS-Tabelle

Hier wird die Quelle der HomeKey-Zutrittsberechtigungen ausgewählt.

* **Nein** (Standard): Es werden die lokal über die USB-Konsole gespeicherten Zuordnungen verwendet.
* **Ja**: Es werden ausschließlich die aktiven Berechtigungen aus der ETS-Tabelle verwendet. Die Bearbeitung erfolgt unter **Allgemein → NFC-Scanner** mit dem Zugangstyp **Apple HomeKey**.

Die ETS-Tabelle bietet 16 Plätze. Jeder aktive Eintrag benötigt eine gültige Issuer-ID, die passende Endpoint-ID und eine eindeutige NFC-Tag-ID. Ungültige oder doppelte aktive Einträge sperren die gesamte HomeKey-ETS-Tabelle. In diesem Fall wird nicht auf lokale USB-Berechtigungen zurückgegriffen.

Eine im Gerät gespeicherte Gerätesperre bleibt auch nach einem erneuten Parameterdownload bestehen. Sie kann nur ausdrücklich über **Erneut zulassen** aufgehoben werden.
