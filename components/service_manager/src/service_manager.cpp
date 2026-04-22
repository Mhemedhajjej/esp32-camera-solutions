#include "service_manager.h"

#include <cstdlib>

#include "esp_log.h"
#include "esp_sleep.h"
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
		auto sendReleaseCaptureFrame = [this](uintptr_t frame_handle) {
			if (frame_handle == 0U) {
				return;
			}

			ServiceCommand release_command{};
			release_command.command_id = static_cast<uint32_t>(ServiceCommandId::ReleaseCaptureFrame);
			release_command.param = static_cast<uint32_t>(frame_handle);
			if (!sendCommand(ComponentId::CameraService, release_command, 0)) {
				ESP_LOGW(TAG, "Failed to send ReleaseCaptureFrame command");
			}
		};

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

		ESP_LOGI(TAG, "Received event: origin=%u id=%u data_ptr=%p",
			static_cast<unsigned int>(event.origin),
			static_cast<unsigned int>(event.event_id),
			reinterpret_cast<void *>(event.data_ptr));

		switch (static_cast<ServiceEventId>(event.event_id)) {
		case ServiceEventId::PowerWakeupCause:
			switch (static_cast<esp_sleep_wakeup_cause_t>(event.data_ptr)) {
			case ESP_SLEEP_WAKEUP_EXT0:
			case ESP_SLEEP_WAKEUP_EXT1:
			case ESP_SLEEP_WAKEUP_TIMER: {
				ServiceCommand capture_command{};
				capture_command.command_id = static_cast<uint32_t>(ServiceCommandId::CaptureFrame);
				if (!sendCommand(ComponentId::CameraService, capture_command, 0)) {
					ESP_LOGW(TAG, "Wakeup trigger detected but failed to send CaptureFrame command");
				} else {
					ESP_LOGI(TAG, "Wakeup trigger detected, sent CaptureFrame command");
				}
				break;
			}

			default:
				ESP_LOGI(TAG, "Power wakeup cause event does not trigger capture");
				break;
			}
			break;

		case ServiceEventId::PowerResetReason:
			ESP_LOGI(TAG, "Power reset reason event received: reason=%u",
				static_cast<unsigned int>(event.data_ptr));
			break;

		case ServiceEventId::CameraFrameReady:
			if (event.data_ptr == 0U) {
				ESP_LOGW(TAG, "Camera frame ready event received with null pointer");
				break;
			}
			{
				CaptureFramePayload *payload = reinterpret_cast<CaptureFramePayload *>(event.data_ptr);
				if (payload == nullptr) {
					ESP_LOGW(TAG, "Camera frame payload is null");
					break;
				}

				ESP_LOGI(TAG, "Camera frame ready event received: frame_len=%u width=%u height=%u format=%u",
					static_cast<unsigned int>(payload->data_len),
					static_cast<unsigned int>(payload->width),
					static_cast<unsigned int>(payload->height),
					static_cast<unsigned int>(payload->format));

				ServiceCommand store_command{};
				store_command.command_id = static_cast<uint32_t>(ServiceCommandId::StoreCapture);
				store_command.param = static_cast<uint32_t>(payload->frame_handle);
				store_command.data_ptr = payload->data_ptr;
				store_command.data_len = payload->data_len;
				store_command.width = payload->width;
				store_command.height = payload->height;
				store_command.format = payload->format;

				const uintptr_t frame_handle = payload->frame_handle;
				std::free(payload);

				if (!sendCommand(ComponentId::StorageService, store_command, 0)) {
					ESP_LOGW(TAG, "Failed to send StoreCapture command to storage service");
					sendReleaseCaptureFrame(frame_handle);
				} else {
					ESP_LOGI(TAG, "Sent StoreCapture command to storage service");
				}
			}
			break;

		case ServiceEventId::CameraError:
			ESP_LOGW(TAG, "Camera error event received: err=%u",
				static_cast<unsigned int>(event.data_ptr));
			break;

		case ServiceEventId::StorageWriteDone:
			ESP_LOGI(TAG, "Storage write done event received: handle=%u",
				static_cast<unsigned int>(event.data_ptr));
			sendReleaseCaptureFrame(event.data_ptr);
			break;

		case ServiceEventId::StorageError:
			ESP_LOGW(TAG, "Storage error event received: handle=%u",
				static_cast<unsigned int>(event.data_ptr));
			sendReleaseCaptureFrame(event.data_ptr);
			break;

		default:
			ESP_LOGI(TAG, "Event received with no handler yet");
			break;
		}

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
