#pragma once

#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace esp32_camera_solutions {

enum class ComponentId : uint8_t {
	Hardware = 0,
	PowerService,
	CameraService,
	WifiService,
	StorageService,
	Count,
};

enum class EventOrigin : uint8_t {
	Unknown = 0,
	Hardware = static_cast<uint8_t>(ComponentId::Hardware),
	PowerService = static_cast<uint8_t>(ComponentId::PowerService),
	CameraService = static_cast<uint8_t>(ComponentId::CameraService),
	WifiService = static_cast<uint8_t>(ComponentId::WifiService),
	StorageService = static_cast<uint8_t>(ComponentId::StorageService),
};

struct ServiceEvent {
	EventOrigin origin = EventOrigin::Unknown;
	uint32_t event_id = 0;
	uintptr_t data_ptr = 0;
};

struct CaptureFramePayload {
	uintptr_t frame_handle = 0;
	uintptr_t data_ptr = 0;
	uint32_t data_len = 0;
	uint16_t width = 0;
	uint16_t height = 0;
	uint32_t format = 0;
};

enum class ServiceEventId : uint32_t {
	Unknown = 0,
	PowerWakeupCause,
	PowerResetReason,
	CameraFrameReady,
	CameraError,
	WifiConnected,
	WifiUploadDone,
	WifiUploadError,
	WifiError,
	StorageWriteDone,
	StorageError,
};

struct ServiceCommand {
	uint32_t command_id = 0;
	uintptr_t data_ptr = 0;
};

enum class ServiceCommandId : uint32_t {
	Unknown = 0,
	EnterSleep,
	CaptureFrame,
	StartStream,
	StopStream,
	UploadCapture,
	StoreCapture,
	ReleaseCaptureFrame,
};

class ServiceManager {
public:
	static ServiceManager &Get();

	bool init(QueueHandle_t *event_queue, QueueHandle_t *command_queues, size_t command_queue_count);
	bool start();

	bool sendCommand(ComponentId component, const ServiceCommand &command, TickType_t timeout_ticks = 0);

	QueueHandle_t *getEventQueue() const;
	QueueHandle_t *getCommandQueue(ComponentId component) const;

private:
	ServiceManager();
	static void taskEntry(void *arg);
	void runTask();

	void handlePowerEvent(const ServiceEvent &event);
	void handleCameraEvent(const ServiceEvent &event);
	void handleWifiEvent(const ServiceEvent &event);
	void handleStorageEvent(const ServiceEvent &event);
	void sendReleaseCaptureFrame(uintptr_t frame_handle);

	QueueHandle_t *event_queue_ = nullptr;
	QueueHandle_t *command_queues_ = nullptr;
	size_t command_queue_count_ = 0;
	uint32_t idle_timeout_ms_ = 0;
	TaskHandle_t task_handle_ = nullptr;
};

using ServiceController = ServiceManager;

} // namespace esp32_camera_solutions
