#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "service_manager.h"

namespace esp32_camera_solutions {

class PowerService {
public:
	static PowerService &Get();

	bool init(QueueHandle_t *event_queue, QueueHandle_t *command_queue);
	bool start();
	bool postEvent(const ServiceEvent &event, TickType_t timeout_ticks = 0);
	bool waitForCommand(ServiceCommand *command, TickType_t timeout_ticks = portMAX_DELAY);

	QueueHandle_t *getEventQueue() const;
	QueueHandle_t *getCommandQueue() const;

private:
	PowerService();
	static void taskEntry(void *arg);
	void runTask();
	void setupWakeupSources() const;

	QueueHandle_t *event_queue_ = nullptr;
	QueueHandle_t *command_queue_ = nullptr;
	TaskHandle_t task_handle_ = nullptr;
	int wakeup_gpio_ = 33;
	bool wakeup_on_high_ = true;
	uint32_t rtc_fallback_timeout_s_ = 3600;
};

} // namespace esp32_camera_solutions
