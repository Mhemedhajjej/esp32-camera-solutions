#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "service_manager.h"

namespace esp32_camera_solutions {

class CameraService {
public:
	static CameraService &Get();

	bool init(QueueHandle_t *event_queue, QueueHandle_t *command_queue);
	bool start();
	bool postEvent(const ServiceEvent &event, TickType_t timeout_ticks = 0);
	bool waitForCommand(ServiceCommand *command, TickType_t timeout_ticks = portMAX_DELAY);

	QueueHandle_t *getEventQueue() const;
	QueueHandle_t *getCommandQueue() const;

private:
	CameraService();
	static void taskEntry(void *arg);
	void runTask();

	QueueHandle_t *event_queue_ = nullptr;
	QueueHandle_t *command_queue_ = nullptr;
	TaskHandle_t task_handle_ = nullptr;
};

} // namespace esp32_camera_solutions
