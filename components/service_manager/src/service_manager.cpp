#include "service_manager.h"

#include "esp_log.h"
#include "freertos/task.h"

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
	ServiceEvent event;
	while (true) {
		if (!waitForEvent(&event, portMAX_DELAY)) {
			continue;
		}

		ESP_LOGI(TAG, "Event received origin=%u id=%u param=%u",
			static_cast<unsigned int>(event.origin),
			static_cast<unsigned int>(event.event_id),
			static_cast<unsigned int>(event.param));
	}
}

bool ServiceManager::waitForEvent(ServiceEvent *event, TickType_t timeout_ticks)
{
	if ((event_queue_ == nullptr) || (*event_queue_ == nullptr) || (event == nullptr)) {
		return false;
	}

	return xQueueReceive(*event_queue_, event, timeout_ticks) == pdTRUE;
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
