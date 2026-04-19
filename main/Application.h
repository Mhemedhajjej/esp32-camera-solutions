#pragma once

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

#include "service_manager.h"

namespace esp32_camera_solutions {

class Application {
public:
    static Application &Get();
    void run();

private:
    Application();
    void initComponents();
    bool initQueues();
    void mainLoop();

    static constexpr size_t kEventQueueLength = 16;
    static constexpr size_t kCommandQueueLength = 8;

    QueueHandle_t event_queue_ = nullptr;
    QueueHandle_t command_queues_[static_cast<size_t>(ComponentId::Count)] = {nullptr};
};

} // namespace esp32_camera_solutions
