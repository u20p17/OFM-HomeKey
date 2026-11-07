### NFC-Tag-ID

Die NFC-Tag-ID (0–1499) entspricht der Spalte **Tag** in der NFC-Zuordnungstabelle. Sie ist **nicht** die Nummer der Aktion und **nicht** die physische UID eines NFC-Tags. Sie bezeichnet den logischen Speicher-/Zuordnungsplatz für einen UID-Tag oder ein Home-Key-Gerät.

Beispiel für eine Türöffnung:

1. Das Gerät in Apple Home hinzufügen und Home Key provisionieren. Falls die Geräte-ID fehlt, iPhone oder Watch einmal am Reader präsentieren und erneut importieren.
2. **HomeKey-Geräte aus Reader einlesen**, die passende Berechtigung benennen und aktivieren.
3. Dieser Berechtigung eine freie **NFC-Tag-ID**, zum Beispiel **100**, geben.
4. Unter **Zuweisung von NFC-Tags zu Aktionen** eine Zeile **Tag 100 → Aktion 1** anlegen.
5. **Aktion 1** mit dem gewünschten Verhalten konfigurieren und ihr Schaltobjekt mit der Gruppenadresse des KNX-Türaktors verbinden.
6. Die geänderten Parameter herunterladen.

Der Weg lautet: **Home-Key-Gerät → NFC-Tag-ID 100 → Tag 100 / Aktion 1 → KNX-Schaltobjekt → Aktor**. Die Kopplung oder der Import allein gewährt noch keinen Zutritt; Sperren und Authentifizierungsbedingungen der Aktion gelten weiterhin.

Jede aktive Home-Key-Berechtigung benötigt eine eigene ID. Eine doppelte ID oder dieselbe Geräteidentität in mehreren aktiven Zeilen sperrt die **gesamte Home-Key-ETS-Tabelle**. ETS prüft dies bereits bei Änderungen und nennt die betroffenen Zeilen. Deaktivierte Entwürfe werden nicht berücksichtigt.

Normale UID-Tags und Home Key teilen sich den ID-Bereich. Reservieren Sie getrennte IDs, etwa UID-Tags 0–99 und Home Key 100–115. Ein bereits belegter UID-Speicherplatz sperrt die Home-Key-Berechtigung mit derselben ID. **Info holen** prüft diese Kollision online. Aktivieren Sie **Konventionelle UID-Tags zulassen**, wenn auch normale Tags am externen Reader genutzt werden.

Mehrere verschiedene IDs dürfen dieselbe Aktion auslösen. Dieselbe Tag-ID darf auch in mehreren NFC-Zuordnungszeilen stehen, um mehrere Aktionen auszulösen. Diese Wiederholung ist beabsichtigt und kein Identitätskonflikt.

**Info holen** zeigt bei Home Key die Bezeichnung aus diesem ETS-Projekt und prüft online, ob das Gerät im Reader provisioniert ist. Die Projektzuordnung wird erst mit dem Download wirksam; eine provisionierte Geräte-ID bestätigt allein noch keine wirksame Zutrittsberechtigung.
