# Результат проверки варианта 20

Ларионов Сергей. Проверено 29 октября 2026 года в Ubuntu 24.04 / WSL2,
GCC 13.3.0. Полный вывод команд сохранён рядом с этим файлом.

| Проверка | Результат |
| --- | --- |
| Сборка с `-Wall -Wextra -Werror` | Успешно |
| Singleton: создание, единственность, запрет копии и перемещения | Успешно |
| Интеграционные тесты | **23 из 23**, `OK` |
| Настоящие libc syslog → journald → rsyslog → отдельный файл | **END-TO-END DEMO PASSED** |
| Facility в настоящем journal | **16 / LOG_LOCAL0**, 26 записей |
| Повторный запуск в демонстрации | PID **694** завершён, запущен PID **697** |
| Остановка | Оба экземпляра записали `STOP signal=15`, PID-файл очищен |

Команды:

```bash
./run_tests.sh
sudo python3 tests/demo_syslog.py
```

Полные результаты: [tests.txt](tests.txt), [syslog-demo.txt](syslog-demo.txt).
Второй прогон выполнялся **без библиотеки перехвата тестового логирования**.

Фрагмент настоящего `/var/log/diskmon.log` (полные строки с датами и путями — в выводе):

```text
diskmon[694]: event=CREATE .../first/proof.txt
diskmon[694]: event=MODIFY .../first/proof.txt
diskmon[694]: event=ACCESS .../first/proof.txt
diskmon[694]: event=MOVED_FROM ... cookie=1991
diskmon[694]: event=MOVED_TO ... cookie=1991
diskmon[694]: event=DELETE .../first/renamed.txt
diskmon[694]: RELOAD_OK ...
diskmon[694]: RELOAD_FAILED keeping previous configuration: Invalid directory at line 1
diskmon[694]: event=CREATE .../second/after-invalid-reload.txt
diskmon[694]: STOP signal=15
diskmon[697]: REPLACE previous_pid=694
diskmon[697]: START pid=697 ...
diskmon[697]: STOP signal=15
```

Проверка показывает работу операций, замены настроек, сохранения старых
наблюдателей при ошибке, замены процесса и завершения. Ограничения самого
`inotify` описаны в основном [README](../README.md).
