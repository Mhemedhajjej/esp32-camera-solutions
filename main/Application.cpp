#include "Application.h"
#include "power_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace esp32_camera_solutions {

static const char *TAG = "Application";

Application::Application() = default;

Application &Application::Get()
{
    static Application instance;
    return instance;
}

void Application::run()
{
    ESP_LOGI(TAG, "Starting ESP32 Camera Solutions v0.1");
    initComponents();
    mainLoop();
}

void Application::initComponents()
{
#if CONFIG_POWER_MANAGER_ENABLE
    PowerManager::Get().init();
#endif
}

void Application::mainLoop()
{
    while (true) {
        ESP_LOGI(TAG, "System is running...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

} // namespace esp32_camera_solutions
