# Trading Engine

Небольшой проект на C++20 для работы со стаканом заявок Kraken.

Клиент подключается к WebSocket API биржи, получает обновления BTC/USD и поддерживает локальный стакан глубиной 10 уровней. После каждого обновления проверяет CRC32, чтобы убедиться, что данные совпадают с состоянием на бирже.

Помимо работы с живыми данными, можно записывать сообщения в файл, воспроизводить их и запускать бенчмарк.

## Сборка

Разрабатывал и тестировал на Ubuntu 24.04 в WSL2.

Зависимости:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build libboost-system-dev libssl-dev libgtest-dev
```

Сборка:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

## Запуск

```bash
./build/kraken_test
```

Клиент подписывается на стакан BTC/USD и выводит лучшие цены покупки и продажи, а также спред между ними. Вывод обновляется только при изменении одной из цен.

Для подключения нужен интернет. API-ключ не требуется.

Если соединение пропадает или CRC32 не совпадает, клиент переподключается и запрашивает новый snapshot. Пока стакан не синхронизирован, обновления не используются.

Для чтения сообщений установлен тайм-аут 10 секунд, для получения первого snapshot — 15 секунд. Между неудачными попытками подключения клиент делает паузу от 1 до 60 секунд.

## Запись и replay

Сообщения Kraken можно записать в файл:

```bash
mkdir -p captures
./build/kraken_test --record captures/btc-usd.ndjson
```

Для остановки нажмите `Ctrl+C`.

Сообщения записываются по одному на строку, в исходном виде. При новом подключении добавляется маркер `{"event":"session"}`. Если файл уже существует, запись продолжается в его конец.

Воспроизведение:

```bash
./build/kraken_test --replay captures/btc-usd.ndjson
```

Replay работает без интернета и обрабатывает сообщения по порядку, без задержек между ними. CRC32 проверяется так же, как при обычном подключении.

В репозитории есть небольшой пример записи:

```bash
./build/kraken_test --replay examples/btc-usd.ndjson
```

## Бенчмарк

```bash
./build/kraken_test --benchmark examples/btc-usd.ndjson 1000
```

Можно использовать и собственную запись.

Перед тестом данные загружаются в память, затем выполняется прогревочный проход.

Бенчмарк измеряет количество обработанных сообщений в секунду и задержки p50, p95, p99. В замеры входят парсинг JSON, обновление стакана и проверка CRC32. Сеть, чтение файла и вывод в консоль не учитываются.

## Тесты

```bash
ctest --test-dir build --output-on-failure --no-tests=error
```

Тесты проверяют работу стакана, JSON-парсера, CRC32, переподключение и replay. Также запускаются проверки на примере записанных данных.

Для тестов используется GoogleTest. Они не требуют подключения к Kraken.

В репозитории настроен GitHub Actions для сборки и запуска тестов на Ubuntu 24.04.

Если тесты не нужны, можно собрать только программу:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build
```

## Структура

- `src/core/` — стакан и обновление ценовых уровней.
- `src/exchange/` — WebSocket-клиент и переподключение.
- `src/market_data/` — парсер, CRC32, синхронизация, запись и replay.
- `src/benchmark.cpp` — бенчмарк.
- `tests/` — тесты.
- `examples/` — пример записанных данных.

## Ссылки

- [Kraken WebSocket v2 Book](https://docs.kraken.com/exchange/api-reference/spot-websocket-v2/book)
- [Book checksum](https://docs.kraken.com/exchange/guides/websockets/book-checksum-v2)
- [Reconnection](https://docs.kraken.com/exchange/guides/websockets/reconnection)