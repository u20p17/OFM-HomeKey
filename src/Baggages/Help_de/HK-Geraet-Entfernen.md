# HomeKey-Gerät löschen

Unter **Allgemein → NFC-Scanner**, Zugangstyp **Apple HomeKey**, den gewünschten
Speicherplatz wählen. Am Reader den Programmiermodus einschalten und **Gerät
löschen und Speicherplatz leeren** ausführen.

Die Geräteschlüssel (öffentlicher Schlüssel und persistenter Authentifizierungsschlüssel)
und lokale Berechtigungen dieses Endpoints werden entfernt. Eine persistente
Sperre für Issuer-ID + Endpoint-ID verhindert, dass erneute Provisionierung,
Neustart oder ein alter ETS-Download den Zugang wiederherstellen. Andere Geräte,
der gemeinsame Issuer und die Reader-Identität bleiben erhalten. Ein iPhone und
eine Watch sind getrennte Endpoints; sie werden getrennt verwaltet.

Erst nach Bestätigung des Readers werden alle passenden ETS-Plätze einschließlich
Bezeichnung geleert. Aktionszuordnungen bleiben bestehen: Wird dieselbe NFC-Tag-ID
später neu vergeben, gelten diese Aktionen für den neuen Zugang.

Der Schlüssel kann weiterhin in Apple Wallet sichtbar sein. Lokales Löschen
entfernt diesen Wallet-Eintrag nicht. Zum Wiederverwenden das gekoppelte Gerät
über **Anlernen / Gerät durch Scan identifizieren** auswählen, danach ausdrücklich
**Gesperrtes Gerät erneut zulassen**. Berechtigung und Aktionen neu prüfen und
Parameter herunterladen. Die Sperre wird nicht durch normalen Import aufgehoben.

Bis zu 128 gesperrte Identitäten werden unabhängig von den 16 ETS-Plätzen
aufbewahrt. Eine volle Sperrliste wird gemeldet; bestehende Sperren werden nicht
verdrängt. Fehler und fehlender Programmiermodus lassen ETS-Plätze unverändert.
