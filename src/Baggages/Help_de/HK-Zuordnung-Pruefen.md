### Zuordnung prüfen

Diese Schaltfläche ist der abschließende Prüfschritt für eine neu eingerichtete oder geänderte HomeKey-Berechtigung. Zuerst am ausgewählten HomeKey-Platz eine Bezeichnung und freie NFC-Tag-ID eintragen und **Berechtigung aktiv → Ja** wählen. Die gewünschten Aktionen im NFC-Tab zuordnen.

Beim Drücken wird der aktuelle UID-Bestand aus dem Leser eingelesen. Die aktiven ETS-Berechtigungen werden auf doppelte Tag-IDs, doppelte Geräteidentitäten und Kollisionen mit UID-Tags geprüft. Zusätzlich wird abgefragt, ob das ausgewählte HomeKey-Gerät im Leser provisioniert oder dauerhaft gesperrt ist. Der Programmiermodus ist dafür nicht erforderlich.

Die Meldung **Zuordnung geprüft** bestätigt diese Prüfung. Die Berechtigung wird dadurch nicht aktiviert oder auf das Gerät übertragen. Anschließend die Parameter mit ETS herunterladen. Eine Türöffnung wird erst durch einen normalen Scan und die passende Aktionszuordnung ausgelöst.

Bei **Gerät ist gesperrt** zuerst unter **Vorgang → anlernen** das Gerät identifizieren und **Erneut zulassen** verwenden. Bei **Gerät ist nicht provisioniert** das gekoppelte iPhone oder die Watch erneut scannen. Nach Änderungen über andere Schnittstellen die Zuordnung erneut prüfen.
