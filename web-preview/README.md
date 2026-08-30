# MCLauncher Web Preview

Статический интерактивный предпросмотр интерфейса Qt-лаунчера. Он не выполняет реальные OAuth, загрузки, установку модов или запуск Minecraft — действия в браузере работают как демонстрационные сценарии.

## Локальный запуск

Из корня workspace:

```bash
python3 -m http.server 4173 --bind 0.0.0.0 --directory mc-launcher/web-preview
```

Откройте `http://localhost:4173`.

В desktop-версии реальные операции реализованы в C++/Qt: Microsoft OAuth, установка vanilla/Fabric/Forge/Quilt, автоматический подбор Java по `javaVersion` выбранного профиля, Modrinth/CurseForge, инстансы, серверы, скины и crash reports.
