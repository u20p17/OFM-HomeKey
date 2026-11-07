### Zuordnung prüfen

Diese Schaltfläche prüft den aktuellen HomeKey-Eintrag vor dem Parameterdownload. Dafür wird der UID-Bestand erneut aus dem Leser eingelesen und mit dem aktuellen ETS-Entwurf verglichen.

Geprüft werden doppelte aktive NFC-Tag-IDs, doppelte Geräteidentitäten, Kollisionen mit UID-Tags sowie die gespeicherte Gerätesperre und die Provisionierung des ausgewählten Geräts. Das Gerät muss über die ETS-KNX-Schnittstelle erreichbar sein.

Die Prüfung speichert keine Zutrittsberechtigung im Leser und löst keine Aktion aus. Prüfen Sie auch die Spalte **Aktionen** in der Übersicht. Eine gültige Berechtigung ohne passende Aktionszuordnung führt noch keine Türöffnung aus.

Nach erfolgreicher Prüfung die Änderungen in ETS speichern und die Parameter herunterladen. Änderungen durch andere Projekte oder Schnittstellen können eine erneute Prüfung erforderlich machen.
