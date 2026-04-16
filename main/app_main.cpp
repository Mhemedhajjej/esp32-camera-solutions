#include "Application.h"

extern "C" void app_main(void)
{
    esp32_camera_solutions::Application::Get().run();
}
