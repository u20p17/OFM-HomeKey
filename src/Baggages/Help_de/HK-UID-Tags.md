### Konventionelle UID-Tags zulassen

Hier können neben Apple Home Key auch herkömmliche NFC-Tags über ihre UID verwendet werden.

* **Nein**: Herkömmliche UID-Tags werden nicht für den Zutritt verwendet.
* **Ja** (Standard): Bekannte UID-Tags werden über die bestehenden NFC-Zuordnungen ausgewertet.

Die UID eines herkömmlichen Tags ist kein kryptografischer Identitätsnachweis. Verwenden Sie diese Freigabe nur, wenn solche Tags ausdrücklich benötigt werden. Ein fehlgeschlagener Home-Key-Authentifizierungsversuch wird nicht als UID-Zutritt behandelt.

NFC-Tag-IDs, die für Home-Key-Berechtigungen verwendet werden, stehen nicht gleichzeitig für UID-Tags zur Verfügung.
