# Mantis Studio Skeleton v0.1 — Start

Dieses Paket enthält den C++23-Quellcode des Architektur-Skeletons, die unveränderte Architekturvorgabe, Tests und Entwicklungsdokumentation.

1. ZIP entpacken und in `mantis-studio` wechseln.
2. Abhängigkeiten nach `BUILDING.md` installieren.
3. Bauen:

```bash
cmake --preset linux-debug -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build --preset linux-debug --parallel 4
ctest --preset linux-debug
```

4. Daemon und Oberfläche aus derselben Shell starten:

```bash
export MANTIS_TOKEN="$(python3 -c 'import secrets; print(secrets.token_hex(24))')"
./build/debug/bin/mantisd --project "$PWD/Demo.mantis" &
./build/debug/bin/mantis-studio
```

5. **Start capture → example → Run pipeline**. Rechts erscheint das PointCloud-Artefakt; die Mitte zeigt die Punktwolke. Für PLY einen neuen Ausgabepfad außerhalb des Projektordners eintragen und **Export selected** wählen.

`crash-test` beendet absichtlich nur den isolierten Plugin-Host. Der Daemon, die Oberfläche und die laufende Aufnahme bleiben im geprüften Referenzablauf aktiv.

Vor dem regulären Beenden die Aufnahme stoppen und anschließend aus einer Shell mit demselben Token ausführen:

```bash
./build/debug/bin/mantis-cli shutdown
```

Die Oberfläche allein zu schließen beendet die Aufnahme ausdrücklich nicht.

**Geprüft:** Ubuntu 24.04 x86_64, Desktop, Qt-freier Headless-Build und ASan/UBSan. **Vorbereitet, hier nicht ausgeführt:** native ARM64-CI. Windows und macOS sind weiterhin Architekturziele ohne nachgewiesenen Build.

Der Referenzalgorithmus verarbeitet absichtlich das erste aufgezeichnete Bild zu synthetischer Geometrie. Echte Scanner-Algorithmen und produktionsreife Mantis-X1-Unterstützung gehören nicht zu diesem Meilenstein. Die konkreten Grenzen stehen in `docs/architecture/milestone.md`.
