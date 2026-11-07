### Gerät löschen

Hier wird das ausgewählte HomeKey-Gerät dauerhaft aus der lokalen Zutrittsverwaltung entfernt. Wählen Sie **Vorgang → löschen**, prüfen Sie **HomeKey-Platz**, Bezeichnung und NFC-Tag-ID und schalten Sie den Programmiermodus am Gerät ein.

**Gerät löschen** entfernt die Geräteschlüssel im Leser und speichert eine dauerhafte Sperre für die Kombination aus Issuer-ID und Endpoint-ID. Erst nach erfolgreicher Bestätigung werden die passenden Plätze in ETS einschließlich Bezeichnung geleert. Bei einem Fehler bleiben die ETS-Einträge erhalten.

Andere Geräte, die gemeinsame Issuer-ID und die Kopplung des Lesers mit Apple Home bleiben erhalten. Ein iPhone und eine Watch sind getrennte Geräte und werden einzeln gelöscht.

Die Sperre bleibt nach Neustart, erneutem Parameterdownload und erneuter Provisionierung dieser Geräte-ID bestehen. Der Schlüssel kann weiterhin in Apple Wallet sichtbar sein; das lokale Löschen entfernt diesen Wallet-Eintrag nicht. Zum Wiederverwenden unter **Vorgang → anlernen** das Gerät scannen und ausdrücklich **Erneut zulassen** wählen.

Aktionszuordnungen für die NFC-Tag-ID werden nicht gelöscht. Wird diese ID später neu vergeben, gelten die bisherigen Zuordnungen auch für den neuen Zugang. Die Übersicht zeigt Aktionszuordnungen ohne aktiven Zugang an.

Es können bis zu 128 gesperrte Geräteidentitäten gespeichert werden. Eine volle Sperrliste wird gemeldet; vorhandene Sperren werden nicht automatisch verdrängt. Den Abschluss in ETS speichern und die geänderten Parameter herunterladen.
