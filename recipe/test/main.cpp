#include <drekar_launch_process_cpp/drekar_launch_process_cpp.h>

int main()
{
    // Construct and destroy without waiting, to check that the library links and loads
    drekar_launch_process_cpp::CWaitForExit wait_exit;
    return 0;
}
