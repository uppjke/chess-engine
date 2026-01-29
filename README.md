# Blitz Chess Engine + UI

Проект: локальный шахматный движок (C++/UCI) и веб‑интерфейс для блица.

## Состав
- engine: C++ UCI движок (CMake).
- ui: локальный браузерный интерфейс.
- bridge: WebSocket мост между UI и движком.

## Сборка движка

### macOS / Linux
1. Установите CMake (если не установлен):
   ```bash
   brew install cmake   # macOS
   sudo apt install cmake  # Ubuntu/Debian
   ```
2. Соберите движок:
   ```bash
   cmake -S ./engine -B ./engine/build
   cmake --build ./engine/build
   ```
После сборки появится `engine/build/blitz_engine`.

### Windows
1. Откройте терминал в корне проекта.
2. Убедитесь, что MinGW доступен:
   - путь `C:\msys64\mingw64\bin` добавлен в PATH
3. Выполните:
   ```cmd
   cmake -S ./engine -B ./engine/build -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM="C:/msys64/mingw64/bin/mingw32-make.exe" -DCMAKE_C_COMPILER="C:/msys64/mingw64/bin/gcc.exe" -DCMAKE_CXX_COMPILER="C:/msys64/mingw64/bin/g++.exe"
   cmake --build ./engine/build
   ```
После сборки появится `engine/build/blitz_engine.exe`.

## Запуск UI + движка
1. Установите зависимости моста:
   - cd ./bridge
   - npm install
2. Запустите мост:
   - npm start
3. Откройте в браузере: http://localhost:3000

## Управление
- Сброс: начальная позиция.
- Откат: отмена хода.
- Перевернуть: смена ориентации.
- Редактор: расстановка фигур + очередь хода + права на рокировку.
- Ход движка: выполнить ход движка вручную.

## Примечания
- UI отправляет позицию в формате FEN в движок.
- Для блица используйте movetime 200–800 мс.
