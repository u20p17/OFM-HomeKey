### Zugänge und Aktionszuordnungen

Die Übersicht kombiniert UID-Tags aus dem letzten vollständig eingelesenen Snapshot mit den HomeKey-Berechtigungen und Aktionszuordnungen des aktuellen ETS-Projekts. Änderungen an HomeKey-Feldern und Aktionszuordnungen werden sofort berücksichtigt; im Gerät werden sie erst nach dem Parameterdownload wirksam.

| Spalte | Bedeutung |
| --- | --- |
| Tag-ID | Gemeinsame NFC-Tag-ID von 0–1499. |
| Typ | UID-Tag oder HomeKey-Gerät. |
| Bezeichnung | Eingelesener UID-Tag-Name oder manuell vergebene HomeKey-Bezeichnung. |
| UID / Issuer + Endpoint | UID des Tags oder vollständige Geräteidentität des HomeKey-Eintrags. |
| Status | Herkunft und Aktivierung des Eintrags sowie Hinweise auf Konflikte oder fehlende Aktionen. |
| Aktionen | Im ETS-Projekt zugeordnete Aktionsnummern. Nicht verfügbare Aktionskanäle werden entsprechend gekennzeichnet. |

Die Zusammenfassung nennt die Anzahl der gefilterten Einträge, Seiten und Konfliktzeilen. Sie weist auch auf Aktionszuordnungen ohne aktiven Zugang hin.

Verschiedene aktive Zugänge benötigen unterschiedliche Tag-IDs. Dieselbe Tag-ID darf mehreren Aktionen zugeordnet sein. Eine als aktiv angezeigte HomeKey-Zeile ist zunächst der ETS-Entwurf; sie bestätigt weder einen erfolgten Download noch, dass das Gerät im Leser provisioniert oder ungesperrt ist. Dafür **Zuordnung prüfen** verwenden.

Prüfen Sie den Zeitpunkt des UID-Snapshots. Änderungen über andere Schnittstellen erfordern ein erneutes Einlesen. Beim Löschen eines Zugangs bleiben Aktionszuordnungen bestehen und gelten bei Wiederverwendung der ID auch für den neuen Zugang.
