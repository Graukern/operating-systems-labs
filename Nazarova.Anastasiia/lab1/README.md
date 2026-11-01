# Лабораторная работа №1

Вариант 20 — Disk monitor.

Демон следит за файлами в каталогах из `disk-monitor.conf` через inotify.
Каждое событие сохраняется в отдельный `.log` и отправляется в syslog (`LOG_LOCAL0`).
Нужны Linux 5.3+, g++ с C++17, rsyslog и Python 3 для тестов.

## Сборка и запуск

```sh
./build.sh
mkdir -p folder1 folder2 logs
./disk-monitor disk-monitor.conf
```

Без аргумента читается `disk-monitor.conf` из текущей папки.
Пути в конфигурации считаются относительно самого файла. Вложенные папки нужно
добавлять отдельной строкой `watch`. Журналы должны находиться вне наблюдаемых папок.

## Системный журнал

```sh
sudo install -m 644 rsyslog/30-disk-monitor.conf /etc/rsyslog.d/
sudo rsyslogd -N1
sudo service rsyslog restart
sudo tail -f /var/log/disk-monitor.log
```

## Управление

```sh
kill -HUP "$(cat disk-monitor.pid)"   # перечитать настройки
kill -TERM "$(cat disk-monitor.pid)" # остановить
```

При повторном запуске старый экземпляр получает SIGTERM. После остановки PID-файл
остаётся пустым. Ошибка в новых настройках не заменяет рабочую конфигурацию.

## Тесты

```sh
DISK_MONITOR_SYSLOG_LOG=/var/log/disk-monitor.log ./tests/run_tests.sh
```

Проверяются конфигурация, файловые события, сигналы, повторный запуск и журнал.

inotify может объединять одинаковые события. При переполнении очереди или потере
наблюдаемого каталога программа пишет ошибку и завершается.
