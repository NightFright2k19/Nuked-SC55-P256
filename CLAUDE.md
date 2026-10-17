# Verbindliche Projektregeln für Änderungen und Dokumentation

Diese Regeln gelten dauerhaft für alle Aufgaben und Änderungen in diesem Repository.
Sie sind proaktiv anzuwenden, ohne dass sie erneut erwähnt werden müssen.

## 1. Dokumentation bei Änderungen

- Bei jeder Änderung prüfen, ob `README.md` und/oder die Autobuild Release Notes
  angepasst werden müssen. Die Release Notes stehen im `body` des Schritts
  "Create GitHub release" in `.github/workflows/build.yml`.
- Erforderliche Dokumentationsänderungen gehören in dieselbe Änderung (denselben PR).
- Keine Dokumentationsanpassung ohne vorherige Prüfung auslassen. Das Ergebnis der
  Prüfung (angepasst oder nicht nötig, mit Grund) im PR festhalten.

## 2. Änderungen an GUI, Setup-Menü oder Patcher

Gilt für Panels und Bedienoberfläche der Plugins (`src/`, Panel-Skripte unter `tools/`),
das SETUP-Menü sowie den Patcher (`patcher.py`, `patcher_i18n.py`).

- Vor der Implementierung Preview-Screenshots erstellen und zur Freigabe vorlegen.
- Mit der Implementierung erst beginnen, wenn die Screenshots ausdrücklich
  freigegeben wurden.
- Nach der Implementierung prüfen, ob bestehende Screenshots in der README
  (`docs/images/` und eingebettete Bilder) veraltet sind, und sie bei Bedarf
  aktualisieren.

## 3. Verbindlichkeit

- Bei Unklarheiten bezüglich der Freigabe vor der Implementierung nachfragen.
