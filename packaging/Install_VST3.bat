@echo off
rem Admiral 1506 - VST3 installieren (als Administrator ausfuehren)
set "TARGET=%CommonProgramFiles%\VST3"
echo Kopiere nach "%TARGET%" ...
xcopy /E /I /Y "%~dp0VST3\Admiral 1506.vst3" "%TARGET%\Admiral 1506.vst3"
if errorlevel 1 (
  echo.
  echo Fehler beim Kopieren. Bitte diese Datei per Rechtsklick "Als Administrator ausfuehren" starten.
) else (
  echo.
  echo Fertig. In FL Studio: Options ^> Manage plugins ^> "Find installed plugins" ^(Verify plugins^).
)
pause
