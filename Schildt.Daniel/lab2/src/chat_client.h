#pragma once

#include <semaphore.h>

int run_client(int id, sem_t *ready);
