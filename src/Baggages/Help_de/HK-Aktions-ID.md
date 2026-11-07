### NFC-Tag-ID

Die NFC-Tag-ID ist die gemeinsame Zuordnungsnummer für UID-Tags und HomeKey-Geräte. Zulässig sind Werte von **0 bis 1499**. Sie entspricht der Spalte **Tag** im NFC-Tab und bezeichnet weder die physische UID noch die Nummer einer Aktion.

Beispiel für eine Türöffnung:

1. Dem HomeKey-Gerät die freie NFC-Tag-ID **100** zuweisen und seine Berechtigung aktivieren.
2. Unter **Zuweisung von NFC-Tags zu Aktionen** die Zuordnung **Tag 100 → Aktion 1** anlegen.
3. Aktion 1 konfigurieren und ihr Schaltobjekt mit der Gruppenadresse des Türaktors verbinden.
4. Die geänderten Parameter herunterladen.

Jeder aktive Zugang benötigt eine eigene NFC-Tag-ID. Doppelte aktive HomeKey-IDs oder Geräteidentitäten sperren die gesamte HomeKey-ETS-Tabelle. Eine Kollision mit einem gespeicherten UID-Tag verhindert den HomeKey-Zutritt über diese ID. Die Übersicht und **Zuordnung prüfen** helfen beim Erkennen solcher Konflikte.

Eine NFC-Tag-ID darf mehreren Aktionen zugeordnet sein. Verschiedene Tag-IDs dürfen ebenfalls dieselbe Aktion auslösen. Diese Aktionszuordnungen sind keine ID-Kollision.

Beim Löschen eines Zugangs bleiben seine Aktionszuordnungen bestehen. Wird die ID später einem neuen Tag oder Gerät zugewiesen, gelten diese Aktionen auch für den neuen Zugang. Unter **Vorgang → löschen** wird die Tag-ID zur Kontrolle schreibgeschützt angezeigt.
