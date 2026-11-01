#include "daemon.hpp"
#include <type_traits>

static_assert(!std::is_default_constructible_v<diskmon::Daemon>);
static_assert(!std::is_copy_constructible_v<diskmon::Daemon>);
static_assert(!std::is_copy_assignable_v<diskmon::Daemon>);
static_assert(!std::is_move_constructible_v<diskmon::Daemon>);

int main() {
    return &diskmon::Daemon::instance() == &diskmon::Daemon::instance() ? 0 : 1;
}
