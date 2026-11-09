# wortUhr ihmSelbst

Meine Wortuhr zeigt die Zeit als Text auf einer Buchstabenmatrix an. Vier LEDs daneben zählen die Minuten zwischen den Fünf-Minuten-Schritten. Über die Weboberfläche stelle ich Farben, Effekte und das Nachtprofil ein. Meine Waschmaschine und die Klingel melden sich über Home Assistant auf der Uhr. Pong, Tetris und ein paar andere Spiele sind auch drin.

## Hardware

Der Sketch ist für einen ESP32-WROOM-32 ausgelegt und erkennt auch den ESP32-S3. Die Matrix hat 11 Spalten und 10 Zeilen. Die 110 LEDs sind zeilenweise im Zickzack angeordnet, gefolgt von den vier Minutenpunkten.

| Anschluss | GPIO im Sketch |
| --- | --- |
| Datenleitung der 114 WS2812B | 18 |
| Taster gegen GND | 19 |
| LDR am Spannungsteiler | 34 |

Beim ESP32-S3 braucht der LDR einen anderen ADC-Pin. Dazu `LDR_PIN` im Sketch an das verwendete Board anpassen. LEDs mit 5 V versorgen und die Masse mit dem ESP32 verbinden. Die Firmware begrenzt den LED-Strom ab Werk rechnerisch auf 1.000 mA.

## Flashen und einrichten

1. In der Arduino IDE das Boardpaket **esp32 by Espressif Systems** installieren und das passende Board wählen.
2. Über den Bibliotheksverwalter **FastLED**, **WiFiManager** (tzapu), **WebSockets** (Markus Sattler) und **MQTT** (Joel Gaehwiler, 256dpi) installieren.
3. [Wortuhr.ino](Wortuhr/Wortuhr.ino) im Ordner `Wortuhr` öffnen und per USB flashen. Falls der Sketch zu groß für die gewählte Partition ist, ein Layout mit größerem App-Slot wählen. Für spätere Updates über das Webinterface muss das Layout OTA unterstützen.
4. Mit dem WLAN `Wortuhr_…` verbinden. Die Uhr zeigt das Passwort an. Unter `http://192.168.4.1` das eigene WLAN auswählen.
5. Danach `http://wortuhr.local` oder die IP-Adresse der Uhr öffnen. Die IP steht auch im seriellen Monitor bei 115200 Baud. Unter `/konfiguration` lassen sich unter anderem WLAN-Portal, Wetterort und MQTT einstellen. Firmware-Updates laufen über `/update`.

Die Weboberfläche läuft per HTTP. Die Uhr im Heimnetz betreiben und keine Ports ins Internet freigeben. Für das WLAN-Portal und den Admin-Zugang eigene Passwörter vergeben.

## Home Assistant und MQTT

In der Uhr MQTT aktivieren und Broker, Zugangsdaten und Basis-Topic eintragen. Der Standard für neu eingerichtete Uhren ist `wortuhr/uhr`. Mit MQTT-Discovery erscheinen die Bedienelemente automatisch in Home Assistant, sofern dort die MQTT-Integration eingerichtet ist.

| Topic | Beispiel |
| --- | --- |
| `wortuhr/uhr/message/set` | `POST IST DA` als Laufschrift |
| `wortuhr/uhr/event/set` | `doorbell` für die Klingel oder `washer:30` für 30 Sekunden Waschmaschinenanzeige |
| `wortuhr/uhr/calendar/set` | `MUELL RAUSSTELLEN` als Terminhinweis |

Wetterdaten kommen direkt von Open-Meteo oder per MQTT. WLAN und Internetzugang werden für die Zeitsynchronisation per NTP benötigt, ein MQTT-Broker ist dafür optional.

## Lizenz

Firmware 4.0.2 · [MIT](LICENSE) · © 2026 ihmSelbst
