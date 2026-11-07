### Apple Home Key

Mit Apple Home Key können unterstützte iPhones und Apple Watches als digitale Schlüssel verwendet werden. Dafür sind eine passende Firmware, ein unterstützter NFC-Leser und eine IP-Verbindung zu Apple Home erforderlich.

Die Einrichtung erfolgt in zwei Bereichen:

* **Apple Home Key**: HomeKey aktivieren, Inbetriebnahme erlauben und die Kopplung mit Apple Home starten. Den privaten Setup-Code zuvor auf der Webseite **Home Key** oder über die lokale USB-Konsole festlegen.
* **Allgemein → NFC-Scanner**: Geräte erkennen oder einlesen, benennen und einer freien NFC-Tag-ID zuordnen. Die Zuordnung zu den gewünschten Aktionen erfolgt anschließend im NFC-Tab.

Die Kopplung mit Apple Home und das Einlesen der Geräte-IDs gewähren allein keinen Zutritt. Hierfür muss eine Berechtigung eingerichtet und auf das Gerät heruntergeladen werden. Die Sperren und Bedingungen der zugeordneten Aktionen gelten weiterhin.

Ohne HomeKey können gespeicherte UID-Tags verwendet werden. Bei aktiviertem HomeKey steuert **Konventionelle UID-Tags zulassen**, ob diese zusätzlich ausgewertet werden.

Das Kommunikationsobjekt **HomeKey Verriegelungsstatus** meldet den tatsächlichen Schlosszustand an Apple Home. Die zusätzlichen HomeKey-Objekte für Pairing, Status und Geräte-IDs werden mit **Rohdaten auf den Bus senden = Ja** eingeblendet. Für die ETS-Schaltflächen sind diese Diagnoseobjekte und ihre Gruppenadressen nicht erforderlich.

#### Kommunikationsobjekte

| Objekt | Bedeutung |
| --- | --- |
| HomeKey Verriegelungsstatus | Tatsächlicher Schlosszustand an Apple Home: 1 = verriegelt, 0 = entriegelt. |
| HomeKey Pairingsteuerung | Pairing-Fenster für fünf Minuten öffnen (1) oder schließen (0). |
| HomeKey Pairingstatus | Pairing-Fenster geöffnet (1) oder geschlossen (0). |
| HomeKey Berechtigungsstatus | Zustand der HomeKey-Berechtigungstabelle. |
| HomeKey Geräteauswahl | Index des im Leser gespeicherten Geräts auswählen (0–63). Dieser Index ist nicht der HomeKey-Platz in ETS. |
| HomeKey Issuer-ID (Teil 1) | Erste acht Hexadezimalzeichen der Issuer-ID des ausgewählten Geräts. |
| HomeKey Issuer-ID (Teil 2) | Letzte acht Hexadezimalzeichen der Issuer-ID des ausgewählten Geräts. |
| HomeKey Geräte-ID | Endpoint-ID des ausgewählten Geräts als zwölf Hexadezimalzeichen. |
| HomeKey Pairing-Befehlsergebnis | Ergebnis des letzten Inbetriebnahmebefehls. |
