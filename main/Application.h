#pragma once

#include "sdkconfig.h"

namespace esp32_camera_solutions {

class Application {
public:
    static Application &Get();
    void run();

private:
    Application();
    void initComponents();
    void mainLoop();
};

} // namespace esp32_camera_solutions
