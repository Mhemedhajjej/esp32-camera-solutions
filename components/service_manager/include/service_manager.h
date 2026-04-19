#pragma once

#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace esp32_camera_solutions {

enum class EventOrigin : uint8_t {
	Unknown = 0,
	Hardware,
	Component,
};

struct ServiceEvent {
	EventOrigin origin = EventOrigin::Unknown;
	uint32_t event_id = 0;
	uint32_t param = 0;
};

enum class ComponentId : uint8_t {
	PowerService = 0,
	Count,
};

struct ServiceCommand {
	uint32_t command_id = 0;
	uint32_t param = 0;
};

class ServiceManager {
public:
	static ServiceManager &Get();

	bool init(QueueHandle_t *event_queue, QueueHandle_t *command_queues, size_t command_queue_count);
	bool start();

	bool waitForEvent(ServiceEvent *event, TickType_t timeout_ticks = portMAX_DELAY);

	bool sendCommand(ComponentId component, const ServiceCommand &command, TickType_t timeout_ticks = 0);

	QueueHandle_t *getEventQueue() const;
	QueueHandle_t *getCommandQueue(ComponentId component) const;

private:
	ServiceManager();
	static void taskEntry(void *arg);
	void runTask();

	QueueHandle_t *event_queue_ = nullptr;
	QueueHandle_t *command_queues_ = nullptr;
	size_t command_queue_count_ = 0;
	TaskHandle_t task_handle_ = nullptr;
};

using ServiceController = ServiceManager;

} // namespace esp32_camera_solutions
