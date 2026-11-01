### Berechtigungen aus ETS-Tabelle

Auswahl der Quelle für die Home-Key-Zutrittsberechtigungen.

* **Nein** (Standard): Es werden die lokal über die USB-Konsole gespeicherten Zuordnungen verwendet.
* **Ja**: Es werden ausschließlich die aktiven Einträge der ETS-Berechtigungstabelle verwendet. Lokale USB-Zuordnungen werden nicht zusätzlich ausgewertet.

Die ETS-Tabelle bietet 16 Einträge. Jeder aktive Eintrag benötigt eine gültige Issuer-ID, eine Endpoint-ID und eine eindeutige logische NFC-Aktions-ID. Bei ungültigen oder doppelten aktiven Einträgen wird die gesamte ETS-Tabelle gesperrt; es wird nicht auf die lokalen Berechtigungen zurückgegriffen.
