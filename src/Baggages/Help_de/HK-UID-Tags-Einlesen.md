### Zugänge einlesen

Die Schaltfläche liest den vollständigen Gerätebestand herkömmlicher UID-Tags, die gespeicherten HomeKey-Berechtigungen einschließlich Bezeichnungen und die heruntergeladenen NFC-Aktionszuordnungen. Das Ergebnis erscheint unter **Datenquelle → Gerätestand**. Eine HomeKey-Geräteidentität, die nur mit Apple Home gekoppelt und noch keiner gespeicherten Berechtigung zugeordnet wurde, wird nicht als zusätzlicher ETS-Zugang angezeigt.

Eine Bezeichnung ohne vollständige Geräte-ID wird nicht als gelerntes HomeKey-Gerät angezeigt. Solche Einträge können unter **ETS-Entwurf** bearbeitet oder gelöscht werden. Ein deaktivierter gespeicherter HomeKey-Zugang mit vollständiger Identität bleibt sichtbar und wird als deaktiviert gekennzeichnet.

Das Einlesen verändert keine Berechtigungen, lernt kein Gerät an und führt keinen Parameterdownload aus. Die Anzeige wird erst nach einem vollständigen, erfolgreichen Lesevorgang übernommen. Bei Abbruch, Verbindungsfehler oder verändertem Datenbestand bleibt der vorherige vollständige Snapshot erhalten.

Verwenden Sie die Funktion nach einem Parameterdownload und nach Änderungen über USB, KNX oder ein anderes Projekt. Passende Firmware mit den erweiterten Snapshot-Funktionen ist erforderlich; der Programmiermodus nicht.
