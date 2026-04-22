#include "Application.h"
#include "camera_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_service.h"
#include "storage_service.h"

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

bool Application::initQueues()
{
    if (event_queue_ == nullptr) {
        event_queue_ = xQueueCreate(kEventQueueLength, sizeof(ServiceEvent));
        if (event_queue_ == nullptr) {
            return false;
        }
    }

    for (size_t i = 0; i < static_cast<size_t>(ComponentId::Count); ++i) {
        if (command_queues_[i] == nullptr) {
            command_queues_[i] = xQueueCreate(kCommandQueueLength, sizeof(ServiceCommand));
            if (command_queues_[i] == nullptr) {
                return false;
            }
        }
    }

    return true;
}

void Application::initComponents()
{
    if (!initQueues()) {
        ESP_LOGE(TAG, "Failed to create application queues");
        return;
    }

    if (!ServiceManager::Get().init(&event_queue_, command_queues_, static_cast<size_t>(ComponentId::Count))) {
        ESP_LOGE(TAG, "Failed to initialize service manager queue bindings");
        return;
    }

    if (!PowerService::Get().init(&event_queue_, &command_queues_[static_cast<size_t>(ComponentId::PowerService)])) {
        ESP_LOGE(TAG, "Failed to initialize power service");
        return;
    }

    if (!CameraService::Get().init(&event_queue_, &command_queues_[static_cast<size_t>(ComponentId::CameraService)])) {
        ESP_LOGE(TAG, "Failed to initialize camera service");
        return;
    }

    if (!StorageService::Get().init(&event_queue_, &command_queues_[static_cast<size_t>(ComponentId::StorageService)])) {
        ESP_LOGE(TAG, "Failed to initialize storage service");
        return;
    }
}

void Application::mainLoop()
{
    while (true) {
        ESP_LOGI(TAG, "System is running...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

} // namespace esp32_camera_solutions
