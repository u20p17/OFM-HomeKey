### Setup-Code

Der private HomeKit-Setup-Code wird für die Kopplung in Apple Home verwendet. Er besteht aus genau acht Ziffern. Codes aus acht gleichen Ziffern sowie die Folgen **12345678** und **87654321** sind nicht zulässig.

Aktivieren Sie den Webserver unter **Netzwerk** und öffnen Sie im Webserver des Geräts die Seite **Home Key**. Aktivieren Sie Apple Home Key und die NFC-Inbetriebnahme in der ETS und schalten Sie den Programmiermodus ein. Ist bereits ein Code gespeichert, geben Sie zusätzlich den bisherigen Code ein. Geben Sie den neuen Code zweimal ein und speichern Sie ihn. Nur beim erstmaligen Festlegen entfällt die Eingabe des bisherigen Codes.

Alternativ kann der Code an der lokalen USB-Konsole mit **hk code &lt;neuer Code&gt; &lt;bisheriger Code&gt;** ersetzt werden. Auch **hk pair** benötigt beim Ersetzen den bisherigen Code. Nach fünf falschen Eingaben ist das Ändern für fünf Minuten gesperrt. Die Fehlversuche bleiben bei einem Neustart erhalten; eine aktive Sperre beginnt danach erneut. **hk reset** löscht den Setup-Code nicht.

Der Code wird im Gerät gespeichert und nicht in der ETS-Anwendung abgelegt oder auf der Webseite zurückgelesen. Verwenden Sie den Webserver nur in einem vertrauenswürdigen Netzwerk; die Verbindung ist nicht verschlüsselt. Das Speichern des Codes öffnet kein Pairing-Fenster und erteilt keine Zutrittsberechtigung.

Öffnen Sie zum Koppeln das Pairing-Fenster in der ETS. Geben Sie auf der Seite **Home Key** unter **In Apple Home hinzufügen** den aktuellen Setup-Code ein und lassen Sie den QR-Code anzeigen. Scannen Sie ihn mit der iPhone-Kamera oder in Apple Home unter **Gerät hinzufügen**. iPhone und Gerät müssen sich im selben Netzwerk befinden. Der QR-Code wird ausschließlich im Browser erzeugt; der gespeicherte Code wird dafür nicht zurückgelesen. Ein falsch eingegebener Code verhindert die Kopplung. Der QR-Code enthält den eingegebenen Setup-Code und darf nicht weitergegeben werden. Die Kopplung allein erteilt keine Zutrittsberechtigung.

Die Gerätekonsole besitzt in dieser Entwicklungsversion keine Anmeldung. Sie zeigt eingegebene Befehle auch anderen verbundenen Konsolen an. Geben Sie dort keine Passwörter oder Setup-Codes ein; verwenden Sie die Seite **Home Key** oder die lokale USB-Konsole. Der Programmiermodus ist keine Bestätigung der physischen Anwesenheit. Deaktivieren Sie den Webserver nach der Inbetriebnahme.
