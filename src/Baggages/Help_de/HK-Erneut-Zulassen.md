### Erneut zulassen

Diese Schaltfläche wird nur benötigt, wenn ein HomeKey-Gerät zuvor mit **Gerät löschen** dauerhaft gesperrt wurde und wieder verwendet werden soll. Für ein neues, noch nie gesperrtes Gerät ist sie nicht erforderlich.

Unter **Vorgang → anlernen** zuerst **Gerät scannen** drücken und das betroffene iPhone oder die Watch vorhalten. Danach den ausgewählten HomeKey-Platz prüfen und **Erneut zulassen** drücken. Hierfür muss **NFC-Inbetriebnahme über USB/KNX erlauben = Ja** sein; der Programmiermodus wird nicht benötigt.

Der Befehl hebt die gespeicherte Sperre für genau diese Kombination aus Issuer-ID und Endpoint-ID auf. Andere Geräte bleiben unverändert. Der ETS-Eintrag wird deaktiviert, damit seine Zuordnung vor einer erneuten Freigabe geprüft werden kann.

Da beim Löschen auch Geräteschlüssel entfernt wurden, das Gerät nach dem Aufheben der Sperre erneut mit **Gerät scannen** erkennen lassen. Anschließend eine freie NFC-Tag-ID vergeben, **Berechtigung aktiv → Ja** wählen, die Aktionszuordnungen kontrollieren, **Zuordnung prüfen** ausführen und die Parameter herunterladen. Das Aufheben der Sperre allein erteilt noch keinen Zutritt.
