### Zugänge und Aktionszuordnungen

Die Übersicht verwendet die ausgewählte **Datenquelle**. Standardmäßig zeigt **Gerätestand** nur den zuletzt vollständig eingelesenen Bestand des Lesers. HomeKey-Geräte, Bezeichnungen, Freigaben und Aktionszuordnungen werden nicht aus unprogrammierten ETS-Eingaben ergänzt. Eine freie Zeile mit einer Bezeichnung allein ist kein eingelesenes Gerät.

Die Quelle **ETS-Entwurf** dient zum Prüfen noch nicht heruntergeladener HomeKey-Berechtigungen und NFC-Aktionszuordnungen. Sie reagiert sofort auf Eingaben. Einträge dieser Ansicht bestätigen weder einen Download noch einen tatsächlichen Zutritt.

| Spalte | Bedeutung |
| --- | --- |
| Tag-ID | Gemeinsame NFC-Tag-ID von 0–1499. |
| Typ | UID-Tag oder HomeKey-Gerät. |
| Bezeichnung | Name aus dem ausgewählten Datenbestand. |
| UID / Issuer + Endpoint | UID oder vollständige HomeKey-Geräteidentität. |
| Status | Herkunft, Freigabe, Provisionierung oder Sperre und mögliche Konflikte. |
| Aktionen | NFC-Aktionsnummern aus dem ausgewählten Datenbestand; nicht verfügbare Kanäle sind gekennzeichnet. |

Nach einem Parameterdownload **Zugänge einlesen** ausführen. Der Gerätestand ist ein Snapshot zum angezeigten Zeitpunkt, keine laufende Verbindung. Bei Lesefehlern bleibt der vorherige vollständige Bestand erhalten; die Statusmeldung weist auf einen möglicherweise veralteten Stand hin.

Beim Löschen bleiben Aktionszuordnungen bestehen. Dieselbe ID darf mehreren Aktionen zugeordnet werden; verschiedene aktive Zugänge benötigen unterschiedliche IDs.
