// Admiral 1506 - Datei per Drag & Drop aus dem Plugin ziehen (Windows)
//
// JUCE startet das Ziehen unter Windows in einem Hintergrund-Thread. In manchen Hosts (z. B. FL Studio)
// bekommt dieser Thread die Maustasten nicht zuverlässig mit - das Ablegen klappt dann nur "zufällig".
// Hier läuft DoDragDrop direkt im Fenster-Thread, so wie beim Ziehen aus dem Explorer.
#pragma once

namespace adwin
{
// Zieht eine Datei (vollständiger Pfad). Blockiert, bis die Maustaste losgelassen wird.
// true = Ziehen wurde durchgeführt (abgelegt oder abgebrochen), false = nicht möglich (dann JUCE-Weg nutzen).
bool dragFile (const wchar_t* path);
}
