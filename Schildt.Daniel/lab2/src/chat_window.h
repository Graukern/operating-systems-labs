#pragma once

#include "chat_session.h"

#include <functional>

int run_chat_window(int argc, char **argv, ChatSession &session,
                    std::function<bool()> client_gone);
