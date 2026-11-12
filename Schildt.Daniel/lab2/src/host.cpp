#include "chat.h"
#include "chat_client.h"
#include "chat_session.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static bool wait_ready(sem_t *sem)
{
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;
    int r;
    do {
        r = sem_timedwait(sem, &ts);
    } while (r < 0 && errno == EINTR);
    return r == 0;
}

static void kill_child(pid_t pid)
{
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

static bool child_gone(pid_t pid)
{
    return waitpid(pid, nullptr, WNOHANG) == pid;
}

int main()
{
    void *mem = mmap(nullptr, sizeof(sem_t), PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        perror("host: mmap");
        return 1;
    }
    sem_t *ready = (sem_t *)mem;
    if (sem_init(ready, 1, 0) < 0) {
        perror("host: sem_init");
        return 1;
    }

    int id = getpid();
    pid_t child = fork();
    if (child < 0) {
        perror("host: fork");
        return 1;
    }
    if (child == 0) {
        int rc = run_client(id, ready);
        fflush(stdout);
        _exit(rc);
    }

    Conn conn(id, true);
    if (!conn.IsOpen()) {
        fprintf(stderr, "host: не удалось создать соединение\n");
        kill_child(child);
        return 1;
    }
    if (!wait_ready(ready)) {
        fprintf(stderr, "host: клиент не ответил за 5 секунд\n");
        kill_child(child);
        return 1;
    }
    printf("host: клиент подключился\n");
    fflush(stdout);

    bool gone = false;
    {
        ChatSession session(conn, "host");
        session.SetOnMessage([](const Message &m) {
            printf("host: %s\n", format_message(m).c_str());
            fflush(stdout);
        });
        session.SetOnStatus([](const std::string &s) {
            printf("host: %s\n", s.c_str());
            fflush(stdout);
        });
        session.Start();
        session.Send(MSG_PUBLIC, "привет");
        int wait_left = 10;
        while (session.Active() && wait_left > 0) {
            if (gone || child_gone(child)) {
                if (!gone)
                    printf("host: клиент завершился\n");
                gone = true;
                wait_left--;
            }
            usleep(100000);
        }
        session.Stop();
    }

    for (int i = 0; i < 20 && !gone; i++) {
        gone = child_gone(child);
        if (!gone)
            usleep(100000);
    }
    if (!gone)
        kill_child(child);
    sem_destroy(ready);
    munmap(mem, sizeof(sem_t));
    return 0;
}
