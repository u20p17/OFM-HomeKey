### Apple Home Key

Mit Apple Home Key können unterstützte iPhones und Apple Watches als digitale Schlüssel verwendet werden. Die angeschlossene NFC-Hardware und die Firmware müssen Apple Home Key unterstützen. Für die Kopplung mit Apple Home wird eine WLAN-Verbindung benötigt.

Die Kopplung in Apple Home allein gewährt keinen Zutritt. Erst die Zuordnung von Issuer-ID und Endpoint-ID zu einer NFC-Tag-ID (Spalte Tag in der NFC-Tabelle) berechtigt das Gerät zum Ausführen der zugehörigen NFC-Aktionen.

Aktivieren Sie Apple Home Key, vergeben Sie einen privaten Setup-Code und öffnen Sie das Pairing-Fenster. Nach der Kopplung können die Geräte-IDs ausgelesen und die Berechtigungen eingerichtet werden.


Ohne Apple Home Key ist die normale UID-Auswertung bei ausgewähltem NFC-Scanner immer aktiv. Bei aktiviertem HomeKey ist **Konventionelle UID-Tags zulassen** standardmäßig **Ja** und kann für reinen HomeKey-Betrieb auf **Nein** gestellt werden. UID-Tags werden nur über gespeicherte NFC-Zuordnungen berechtigt; eine UID ist kein kryptografischer Identitätsnachweis.

Das Objekt **HomeKey Verriegelungsstatus** übermittelt den tatsächlichen Schlosszustand an Apple Home und bleibt bei aktiviertem Home Key sichtbar. Die acht weiteren Home-Key-Objekte für KNX-Inbetriebnahme, Status und Geräte-IDs erscheinen nur bei **Rohdaten auf den Bus senden = Ja**. Ohne diese Freigabe sendet die Firmware keine entsprechenden Status- oder Geräte-ID-Telegramme und nimmt keine Inbetriebnahme-/Endpoint-Auswahlbefehle über diese Objekte an.

Die ETS-Schaltflächen für Status, Pairing und Geräteimport verwenden Online-Verwaltungsfunktionen und benötigen keine Gruppenadressen dieser Diagnoseobjekte. Die NFC-Aktionen und deren Schaltobjekte arbeiten unabhängig davon.


### HomeKey-Kommunikationsobjekte

| Objektname | Bedeutung |
| --- | --- |
| HomeKey Verriegelungsstatus | Rückmeldung des Schlosses an Apple Home: 1 = verriegelt, 0 = entriegelt. |
| HomeKey Pairingsteuerung | Pairing-Fenster für fünf Minuten öffnen (1) oder schließen (0). |
| HomeKey Pairingstatus | Pairing-Fenster geöffnet (1) oder geschlossen (0). |
| HomeKey Berechtigungsstatus | Status der HomeKey-Berechtigungstabelle. |
| HomeKey Geräteauswahl | Index des provisionierten Geräts auswählen (0–63). |
| HomeKey Issuer-ID (Teil 1) | Erste acht Hex-Zeichen der Issuer-ID des ausgewählten Geräts. |
| HomeKey Issuer-ID (Teil 2) | Letzte acht Hex-Zeichen der Issuer-ID des ausgewählten Geräts. |
| HomeKey Geräte-ID | Endpoint-ID des ausgewählten Geräts als zwölf Hex-Zeichen. |
| HomeKey Pairing-Befehlsergebnis | Ergebnis des Inbetriebnahmebefehls. |
