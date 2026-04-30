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
	ServiceEvent event;
	ServiceCommand sleep_command{};
	const TickType_t wait_ticks = (idle_timeout_ms_ == 0U)
		? portMAX_DELAY
		: pdMS_TO_TICKS(idle_timeout_ms_);

	
	if ((event_queue_ == nullptr) || (*event_queue_ == nullptr)) {
		ESP_LOGE(TAG, "Event queue is invalid, service task stopping");
		vTaskDelete(nullptr);
		return;
	}

	while (true) {
		if (xQueueReceive(*event_queue_, &event, wait_ticks) != pdTRUE) {
			/* idle timeout reached without new events */
			sleep_command = {};
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

		/* Dispatch event to appropriate handler based on origin */
		switch (static_cast<ComponentId>(event.origin)) {
		case ComponentId::Hardware:
			handlePowerEvent(event);
			break;

		case ComponentId::PowerService:
			handlePowerEvent(event);
			break;

		case ComponentId::CameraService:
			handleCameraEvent(event);
			break;

		case ComponentId::WifiService:
			handleWifiEvent(event);
			break;

		case ComponentId::StorageService:
			handleStorageEvent(event);
			break;

		default:
			ESP_LOGI(TAG, "Event received with no handler yet");
			break;
		}
	}
}

void ServiceManager::sendReleaseCaptureFrame(uintptr_t frame_handle)
{
	ServiceCommand release_command{
		.command_id = static_cast<uint32_t>(ServiceCommandId::ReleaseCaptureFrame),
		.data_ptr = frame_handle,
	};
	
	if (frame_handle == 0U) {
		return;
	}

	/* send ReleaseCaptureFrame command to camera service to release the frame buffer */
	if (!sendCommand(ComponentId::CameraService, release_command, 0)) {
		ESP_LOGW(TAG, "Failed to send ReleaseCaptureFrame command");
	}
}

void ServiceManager::handlePowerEvent(const ServiceEvent &event)
{
	ServiceCommand capture_command{
		.command_id = static_cast<uint32_t>(ServiceCommandId::CaptureFrame),
		.data_ptr = 0U,
	};

	/* For power events, we check if the wakeup cause is one that should trigger a capture */
	switch (static_cast<ServiceEventId>(event.event_id)) {
	case ServiceEventId::PowerWakeupCause:
		switch (static_cast<esp_sleep_wakeup_cause_t>(event.data_ptr)) {
		case ESP_SLEEP_WAKEUP_EXT0:
		case ESP_SLEEP_WAKEUP_EXT1:
		case ESP_SLEEP_WAKEUP_TIMER: {
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
	
	/* For power reset reason events, we just log the reason for now */
	case ServiceEventId::PowerResetReason:
		ESP_LOGI(TAG, "Power reset reason event received: reason=%u",
			static_cast<unsigned int>(event.data_ptr));
		break;

	default:
		break;
	}
}

void ServiceManager::handleCameraEvent(const ServiceEvent &event)
{
	CaptureFramePayload *payload = nullptr;
	ServiceCommand upload_command{};
	uintptr_t frame_handle = 0U;

	switch (static_cast<ServiceEventId>(event.event_id)) {
	case ServiceEventId::CameraFrameReady: {
		if (event.data_ptr == 0U) {
			ESP_LOGW(TAG, "Camera frame ready event received with null pointer");
			break;
		}

		/* fetch camera frame payload */
		payload = reinterpret_cast<CaptureFramePayload *>(event.data_ptr);
		ESP_LOGI(TAG, "Camera frame ready: frame_len=%u width=%u height=%u format=%u",
			static_cast<unsigned int>(payload->data_len),
			static_cast<unsigned int>(payload->width),
			static_cast<unsigned int>(payload->height),
			static_cast<unsigned int>(payload->format));

		/* send command to wifi service to upload the captured frame */
		upload_command = {};
		upload_command.command_id = static_cast<uint32_t>(ServiceCommandId::UploadCapture);
		upload_command.data_ptr = event.data_ptr;
		
		/* fetch frame handle from payload */
		frame_handle = payload->frame_handle;

		if (!sendCommand(ComponentId::WifiService, upload_command, 0)) {
			ESP_LOGW(TAG, "Failed to send UploadCapture command to wifi service");
			std::free(payload);
			sendReleaseCaptureFrame(frame_handle);
		} else {
			ESP_LOGI(TAG, "Sent UploadCapture command to wifi service");
		}
		break;
	}

	case ServiceEventId::CameraError:
		ESP_LOGW(TAG, "Camera error event received: err=%u",
			static_cast<unsigned int>(event.data_ptr));
		break;

	default:
		break;
	}
}

void ServiceManager::handleWifiEvent(const ServiceEvent &event)
{
	switch (static_cast<ServiceEventId>(event.event_id)) {
	case ServiceEventId::WifiConnected:
		ESP_LOGI(TAG, "WiFi connected");
		break;

	case ServiceEventId::WifiUploadDone:
		ESP_LOGI(TAG, "WiFi upload done: handle=%u",
			static_cast<unsigned int>(event.data_ptr));
		sendReleaseCaptureFrame(event.data_ptr);
		break;

	case ServiceEventId::WifiUploadError:
		ESP_LOGW(TAG, "WiFi upload error: handle=%u",
			static_cast<unsigned int>(event.data_ptr));
		sendReleaseCaptureFrame(event.data_ptr);
		break;

	case ServiceEventId::WifiError:
		ESP_LOGW(TAG, "WiFi service reported an error");
		break;

	default:
		break;
	}
}

void ServiceManager::handleStorageEvent(const ServiceEvent &event)
{
	switch (static_cast<ServiceEventId>(event.event_id)) {
	case ServiceEventId::StorageWriteDone:
		ESP_LOGI(TAG, "Storage write done: handle=%u",
			static_cast<unsigned int>(event.data_ptr));
		sendReleaseCaptureFrame(event.data_ptr);
		break;

	case ServiceEventId::StorageError:
		ESP_LOGW(TAG, "Storage error event received: handle=%u",
			static_cast<unsigned int>(event.data_ptr));
		sendReleaseCaptureFrame(event.data_ptr);
		break;

	default:
		break;
	}
}

bool ServiceManager::sendCommand(ComponentId component, const ServiceCommand &command, TickType_t timeout_ticks)
{
	const size_t index = componentToIndex(component);
	QueueHandle_t *queue = nullptr;

	if ((command_queues_ == nullptr) || (index >= command_queue_count_)) {
		return false;
	}

	queue = &command_queues_[index];
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
