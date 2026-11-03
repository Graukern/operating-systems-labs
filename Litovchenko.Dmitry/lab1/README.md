# Лабораторная работа №1. Disk monitor (вариант 20)

Демон следит через inotify за каталогами из конфига и пишет каждое событие с файлом в syslog
с facility `LOG_LOCAL0`, rsyslog складывает их в `/var/log/diskmon.log`. Сообщения самого
демона (запуск, ошибки, остановка) идут в `LOG_DAEMON`.

## Сборка и запуск

```bash
./build.sh
sudo cp rsyslog/30-diskmon.conf /etc/rsyslog.d/ && sudo systemctl restart rsyslog
sudo cp logrotate/diskmon /etc/logrotate.d/

./diskmon [-f] [-p PIDFILE] [-c] [CONFIG]   # CONFIG по умолчанию ./diskmon.conf
```

- `-f` — не демонизироваться, `-p` — путь к pid-файлу, `-c` — только проверить конфиг.
- `SIGHUP` — перечитать конфиг (при ошибке остаётся старый), `SIGUSR1` — статистика, `SIGTERM` — выход.

## Конфиг

```
debounce_ms 500
watch demo/inbox  recursive=yes exclude=*.swp,.git
watch demo/shared recursive=no  events=create,delete,move
```

`events`: `access,modify,attrib,open,close,create,delete,move` или `all`. Относительные пути
считаются от файла конфига. `debounce_ms` склеивает серию `MODIFY` одного файла в одну строку.

## Лог

```
diskmon[812]: event=CREATE kind=file path=/srv/data/report.txt
diskmon[812]: event=MODIFY kind=file path=/srv/data/app.log count=37
diskmon[812]: event=RENAME kind=dir from=/srv/data/old to=/srv/data/new
```

## Тесты

```bash
./tests/run_tests.sh
```

Root и rsyslog не нужны: `syslog()`
перехватывается через `LD_PRELOAD` (`tests/syslog_capture.cpp`) вместе с facility.

## Ограничения

- inotify не сообщает, кто обратился к файлу (pid/uid), для этого нужен fanotify.
- Файлы, созданные в новом подкаталоге до установки на него watch, могут быть пропущены.
