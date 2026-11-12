#include "chat_window.h"

#include <QApplication>
#include <QBrush>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

static void add_line(QListWidget *list, qint64 time_ms, const QString &text, bool service)
{
    QListWidgetItem *item = new QListWidgetItem(text);
    item->setData(Qt::UserRole, time_ms);
    if (service)
        item->setForeground(QBrush(Qt::gray));
    int pos = list->count();
    while (pos > 0 && list->item(pos - 1)->data(Qt::UserRole).toLongLong() > time_ms)
        pos--;
    list->insertItem(pos, item);
    list->scrollToBottom();
}

int run_chat_window(int argc, char **argv, ChatSession &session,
                    std::function<bool()> client_gone)
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("Чат (хост)");
    window.resize(500, 400);

    QLabel *status = new QLabel("Статус: ожидание");
    QListWidget *list = new QListWidget;
    QLineEdit *input = new QLineEdit;
    input->setMaxLength(90);
    input->setPlaceholderText("Сообщение");
    QPushButton *all_btn = new QPushButton("Всем");
    QPushButton *private_btn = new QPushButton("Лично");

    QHBoxLayout *row = new QHBoxLayout;
    row->addWidget(input);
    row->addWidget(all_btn);
    row->addWidget(private_btn);
    QVBoxLayout *col = new QVBoxLayout(&window);
    col->addWidget(status);
    col->addWidget(list);
    col->addLayout(row);

    auto show_status = [status, list](const QString &text) {
        status->setText("Статус: " + text);
        add_line(list, QDateTime::currentMSecsSinceEpoch(), "* " + text, true);
    };

    auto send = [&](int type) {
        QString text = input->text().trimmed();
        if (text.isEmpty() || !session.Active())
            return;
        std::string str = text.toUtf8().constData();
        Message m = make_message(type, "host", str);
        add_line(list, m.time_ms, QString::fromUtf8(format_message(m).c_str()), false);
        session.Send(type, str);
        input->clear();
    };

    QObject::connect(all_btn, &QPushButton::clicked, [&]() { send(MSG_PUBLIC); });
    QObject::connect(private_btn, &QPushButton::clicked, [&]() { send(MSG_PRIVATE); });
    QObject::connect(input, &QLineEdit::returnPressed, [&]() { send(MSG_PUBLIC); });

    session.SetOnMessage([&window, list](const Message &m) {
        Message copy = m;
        QMetaObject::invokeMethod(&window, [list, copy]() {
            add_line(list, copy.time_ms,
                     QString::fromUtf8(format_message(copy).c_str()), false);
        }, Qt::QueuedConnection);
    });
    session.SetOnStatus([&window, show_status](const std::string &s) {
        QString text = QString::fromUtf8(s.c_str());
        QMetaObject::invokeMethod(&window, [show_status, text]() {
            show_status(text);
        }, Qt::QueuedConnection);
    });

    bool announced = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        bool ended = !session.Active();
        if (client_gone()) {
            if (!announced) {
                announced = true;
                show_status("клиент завершился");
            }
            ended = true;
        }
        if (ended) {
            input->setEnabled(false);
            all_btn->setEnabled(false);
            private_btn->setEnabled(false);
        }
    });
    timer.start(200);

    show_status("клиент подключился");
    session.Start();
    window.show();
    int rc = app.exec();
    session.Stop();
    return rc;
}
