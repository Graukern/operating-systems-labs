#include "conn.h"

int main()
{
    Conn conn(1, true);
    return conn.IsOpen() ? 0 : 1;
}
