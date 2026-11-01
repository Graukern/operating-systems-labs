# Лабораторная работа 1

## Задание
Напоминалка (вариант 19). В конфигурационный файл пользователь может добавлять с помощью специального скрипта новые события для напоминания в формате “add_event date time [flag] text”, где flag - необязательное поле, значения “-h -d -w” означают повтор напоминаний каждый час, день, неделю, соответственно. В указанное время демон должен открывать новый терминал и выводить заданный текст.
Интервал срабатывания: 10 секунд.

## Сборка и запуск

Все команды выполняются из корневой директории проекта (там, где лежат `build.sh` и `config.ini`).

```bash
chmod +x build.sh add_event.sh run_tests.sh tests/test_daemon.sh
./build.sh
./reminderd config.ini
./add_event.sh config.ini 2026-10-05 09:00:00 -d "Homework"
kill -TERM $(cat /tmp/reminderd_$(id -u).pid)
journalctl -t reminderd -f
```

Формат конфига:

```ini
[settings]
interval = 10
terminal = xterm -e sh -c

[events]
2026-10-05 09:00:00 -d Morning workout
```
##Тесты
Для запуска всех тестов (модульных и интеграционных) выполните:

```bash
./run_tests.sh
```