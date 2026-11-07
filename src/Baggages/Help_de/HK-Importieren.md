### Geräte einlesen

Diese Schaltfläche liest alle im Leser gespeicherten HomeKey-Geräte über die ETS-KNX-Schnittstelle ein. Sie ist unter **Zugangstyp → Apple HomeKey** und **Vorgang → anlegen** verfügbar. Das iPhone oder die Watch muss beim Einlesen nicht am Leser vorgehalten werden. Der Programmiermodus ist nicht erforderlich.

Neue Issuer- und Endpoint-IDs werden in vollständig leere Plätze der ETS-Tabelle übernommen. Vorhandene Einträge und Bezeichnungen bleiben erhalten. Reichen die freien Plätze nicht aus, wird der Import ohne Änderungen abgebrochen.

Neue Einträge sind deaktiviert und erhalten zunächst die NFC-Tag-ID 0. Anschließend die Geräte benennen, jeweils eine freie Tag-ID vergeben, die gewünschten Berechtigungen aktivieren und die Aktionen zuordnen. **Zuordnung prüfen** ausführen und die geänderten Parameter herunterladen.

Fehlt ein bereits gekoppeltes Gerät, verwenden Sie **Vorgang → anlernen → Gerät scannen** und halten Sie es vor den Leser. Das Einlesen hebt keine Gerätesperren auf und erteilt allein keinen Zutritt.
