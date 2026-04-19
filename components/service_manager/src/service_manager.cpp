#include "service_manager.h"

#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef CONFIG_SERVICE_MANAGER_IDLE_TIMEOUT_MS
#define CONFIG_SERVICE_MANAGER_IDLE_TIMEOUT_MS 30000
#endif

namespace esp32_camera_solutions {

static const char *TAG = "service_manager";

namespace {

static size_t componentToIndex(ComponentId component)
{
	return static_cast<size_t>(component);
}

} // namespace

ServiceManager::ServiceManager() = default;

ServiceManager &ServiceManager::Get()
{
	static ServiceManager instance;
	return instance;
}

bool ServiceManager::init(QueueHandle_t *event_queue, QueueHandle_t *command_queues, size_t command_queue_count)
{
	if ((event_queue == nullptr) || (command_queues == nullptr)) {
		return false;
	}

	if (command_queue_count < static_cast<size_t>(ComponentId::Count)) {
		return false;
	}

	event_queue_ = event_queue;
	command_queues_ = command_queues;
	command_queue_count_ = command_queue_count;
	idle_timeout_ms_ = CONFIG_SERVICE_MANAGER_IDLE_TIMEOUT_MS;
	if (*event_queue_ == nullptr) {
		return false;
	}

	return start();
}

bool ServiceManager::start()
{
	if (task_handle_ != nullptr) {
		return true;
	}

	const BaseType_t result = xTaskCreate(&ServiceManager::taskEntry,
		"service_manager",
		4096,
		this,
		tskIDLE_PRIORITY + 2,
		&task_handle_);

	if (result != pdPASS) {
		task_handle_ = nullptr;
		ESP_LOGE(TAG, "Failed to create service manager task");
		return false;
	}

	return true;
}

void ServiceManager::taskEntry(void *arg)
{
	static_cast<ServiceManager *>(arg)->runTask();
}

void ServiceManager::runTask()
{
	if ((event_queue_ == nullptr) || (*event_queue_ == nullptr)) {
		ESP_LOGE(TAG, "Event queue is invalid, service task stopping");
		vTaskDelete(nullptr);
		return;
	}

	const TickType_t wait_ticks = (idle_timeout_ms_ == 0U)
		? portMAX_DELAY
		: pdMS_TO_TICKS(idle_timeout_ms_);

	ServiceEvent event;
	while (true) {
		if (xQueueReceive(*event_queue_, &event, wait_ticks) != pdTRUE) {
            ServiceCommand sleep_command{};
            sleep_command.command_id = static_cast<uint32_t>(ServiceCommandId::EnterSleep);
            if (!sendCommand(ComponentId::PowerService, sleep_command, 0)) {
                ESP_LOGW(TAG, "Idle timeout reached but failed to send EnterSleep command");
            } else {
                ESP_LOGI(TAG, "Idle timeout reached, sent EnterSleep command");
            }
			continue;
		}

        ESP_LOGI(TAG, "Received event: origin=%u id=%u param=%u",
            static_cast<unsigned int>(event.origin),
            static_cast<unsigned int>(event.event_id),
            static_cast<unsigned int>(event.param));
	}
}

bool ServiceManager::sendCommand(ComponentId component, const ServiceCommand &command, TickType_t timeout_ticks)
{
	const size_t index = componentToIndex(component);
	if ((command_queues_ == nullptr) || (index >= command_queue_count_)) {
		return false;
	}

	QueueHandle_t *queue = &command_queues_[index];
	if (*queue == nullptr) {
		return false;
	}

	return xQueueSend(*queue, &command, timeout_ticks) == pdTRUE;
}

QueueHandle_t *ServiceManager::getEventQueue() const
{
	return event_queue_;
}

QueueHandle_t *ServiceManager::getCommandQueue(ComponentId component) const
{
	const size_t index = componentToIndex(component);
	if ((command_queues_ == nullptr) || (index >= command_queue_count_)) {
		return nullptr;
	}

	return &command_queues_[index];
}

} // namespace esp32_camera_solutions
