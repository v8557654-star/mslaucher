# MCLauncher

Рабочий каркас лаунчера **Minecraft: Java Edition** на C++/Qt 6 для Windows 10/11. Лаунчер использует только официальные manifest/download/API Minecraft и вход Microsoft: обход лицензии, пиратский/offline-режим и подмена аккаунтов намеренно не реализованы.

## Что уже работает

- вход Microsoft Device Code Flow;
- обмен Microsoft → Xbox Live → XSTS → Minecraft Services;
- проверка наличия профиля Minecraft Java Edition;
- сохранение refresh token через Windows DPAPI;
- загрузка официального `version_manifest_v2.json`;
- выбор release/snapshot версии;
- загрузка и проверка SHA-1 для client.jar, библиотек, natives, asset index, ресурсов и log config;
- возобновление незавершённых загрузок через `.part` и HTTP Range;
- автоматическая установка Fabric Loader через Fabric Meta API;
- автоматическая установка Quilt Loader через Quilt Meta API;
- автоматическая установка Forge через официальный Forge installer;
- отдельные инстансы с разными папками игры;
- поиск и установка модов из Modrinth вместе с обязательными зависимостями;
- установка `.mrpack`-модпаков Modrinth с файлами и overrides;
- поиск и установка CurseForge-модов через официальный Core API;
- защищённое поле CurseForge API key в настройках (также поддерживаются `MCL_CURSEFORGE_API_KEY` и `CURSEFORGE_API_KEY`);
- лента официальных новостей Mojang с кэшем;
- загрузка PNG-скина и сброс скина через Minecraft Services;
- сохранённая адресная книга серверов с подстановкой в быстрое подключение;
- просмотр локальных Minecraft/JVM crash reports и открытие папки отчётов;
- быстрый запуск сразу на выбранный сервер;
- распаковка natives из ZIP без внешней утилиты;
- построение JVM/game arguments для современных и старых форматов version JSON;
- запуск Java-процесса с логом stdout/stderr;
- плавные анимации вкладок, меню и фонового градиента в Qt-интерфейсе;
- выбор папки игры и Java, память, разрешение окна;
- список модов в папке `mods`, быстрый доступ к `mods` и папке игры;
- кэш манифеста на случай временной недоступности сети.

## Важно про «все функции»

Часть возможностей коммерческих лаунчеров всё ещё требует отдельной большой подсистемы: расширенный редактор профилей, автообновление самого лаунчера, sandbox, встроенный ping/favicon серверов и телеметрия. В репозитории уже есть рабочие vanilla/Fabric/Quilt/Forge-профили, инстансы, Modrinth, CurseForge, `.mrpack`, новости, скины, адресная книга серверов, crash-report viewer и быстрый серверный запуск; остальные расширения можно добавлять поверх этого ядра.

## Сборка в Windows

1. Установите Qt 6.4+ с компонентом **MSVC 2022 64-bit** и Visual Studio 2022 с Desktop C++.
2. Откройте `x64 Native Tools Command Prompt for VS 2022`.
3. Выполните:

```bat
cmake -S mc-launcher -B mc-launcher/build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\msvc2022_64
cmake --build mc-launcher/build --config Release
C:\Qt\6.8.0\msvc2022_64\bin\windeployqt.exe mc-launcher\build\Release\MCLauncher.exe
```

Путь к Qt замените на свой. Библиотека `miniz` уже лежит в `third_party/miniz`, отдельный пакет не нужен.

## Первый запуск

1. Установите JDK 8, 17, 21 или другую версию, которая требуется выбранному Minecraft. Поле Java можно оставить пустым: лаунчер прочитает `javaVersion.majorVersion` из профиля Minecraft и автоматически выберет совместимый `java.exe`.
2. Если подходящая Java не найдена, лаунчер не запустит игру на несовместимой версии и покажет, какой JDK нужно установить. Путь к конкретному `java.exe` можно указать вручную — он тоже проверяется по major-версии.
3. Нажмите **Войти через Microsoft**. Откроется страница Microsoft, код будет автоматически скопирован в буфер обмена.
4. Выберите версию и нажмите **Установить / обновить**.
5. После полной загрузки нажмите **Играть**.

Папка по умолчанию в Windows: `%APPDATA%\.minecraft`.

## OAuth client id

По умолчанию используется публичный client id стандартного Minecraft/Xbox sign-in flow:

```text
00000000402b5328
```

Если Microsoft изменит доступность этого client id, можно указать свой public client id через переменную окружения:

```bat
set MCL_OAUTH_CLIENT_ID=ВАШ_CLIENT_ID
```

В Azure приложение должно поддерживать public client/device-code flow и scope `XboxLive.signin offline_access`. Секрет клиента в лаунчере не хранится.

## Структура

```text
src/
  auth.*          Microsoft/Xbox/Minecraft Services authentication
  http.*          Qt network requests, SHA-1 downloads, resume
  minecraft.*     official catalog, installer, assets, libraries and loaders
  modrinth.*      Modrinth search, mods, modpacks and dependencies
  curseforge.*    CurseForge Core API search and JAR installation
  skin.*          Minecraft Services skin operations
  launcher.*      JVM/classpath/argument builder
  main_window.*   Qt GUI
  zip_extract.*   safe native ZIP extraction
third_party/miniz/ miniz public-domain ZIP/DEFLATE implementation
```

## Web preview

В `web-preview/index.html` находится интерактивный статический предпросмотр интерфейса. Он показывает вкладки запуска, инстансов, модов, серверов, новостей, диагностики и аккаунта; сетевые операции и запуск Minecraft в нём имитируются.

```bash
python3 -m http.server 4173 --bind 0.0.0.0 --directory web-preview
```

## Сборка Windows через GitHub Actions

В репозитории есть workflow `.github/workflows/build-windows.yml`. После загрузки проекта на GitHub:

1. Откройте вкладку **Actions**.
2. Выберите **Build MCLauncher for Windows**.
3. Нажмите **Run workflow** или сделайте push в `main`/`master`.
4. Скачайте artifact `MCLauncher-windows-x64` — внутри находится portable ZIP с `MCLauncher.exe` и Qt DLL.

Если создать тег вида `v0.1.0`, тот же ZIP дополнительно прикрепится к GitHub Release.

## Лицензия

Код лаунчера распространяется как пример под MIT. `third_party/miniz` — public domain / лицензия, указанная в заголовке исходника.
