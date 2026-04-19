#include "power_service.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace esp32_camera_solutions {

static const char *TAG = "power_service";

PowerService::PowerService() = default;

PowerService &PowerService::Get()
{
	static PowerService instance;
	return instance;
}

bool PowerService::init(QueueHandle_t *event_queue, QueueHandle_t *command_queue)
{
	if ((event_queue == nullptr) || (command_queue == nullptr)) {
		return false;
	}

	event_queue_ = event_queue;
	command_queue_ = command_queue;
	if ((*event_queue_ == nullptr) || (*command_queue_ == nullptr)) {
		return false;
	}

	wakeup_gpio_ = CONFIG_POWER_SERVICE_WAKEUP_GPIO;
	wakeup_on_high_ = CONFIG_POWER_SERVICE_WAKEUP_LEVEL;
	rtc_fallback_timeout_s_ = CONFIG_POWER_SERVICE_RTC_FALLBACK_TIMEOUT_S;

	return start();
}

void PowerService::setupWakeupSources() const
{
	const gpio_num_t wakeup_gpio = static_cast<gpio_num_t>(wakeup_gpio_);
	const int wakeup_level = wakeup_on_high_ ? 1 : 0;

	gpio_reset_pin(wakeup_gpio);
	gpio_set_direction(wakeup_gpio, GPIO_MODE_INPUT);
	if (wakeup_on_high_) {
		gpio_pulldown_en(wakeup_gpio);
		gpio_pullup_dis(wakeup_gpio);
	} else {
		gpio_pullup_en(wakeup_gpio);
		gpio_pulldown_dis(wakeup_gpio);
	}

	ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(wakeup_gpio, wakeup_level));

	if (rtc_fallback_timeout_s_ > 0U) {
		ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(rtc_fallback_timeout_s_) * 1000000ULL));
		ESP_LOGI(TAG, "Configured RTC fallback wakeup after %u seconds", static_cast<unsigned int>(rtc_fallback_timeout_s_));
	}

	ESP_LOGI(TAG,
		"Configured GPIO wakeup on GPIO %d level=%s",
		wakeup_gpio_,
		wakeup_on_high_ ? "HIGH" : "LOW");
}

bool PowerService::start()
{
	if (task_handle_ != nullptr) {
		return true;
	}

	const BaseType_t result = xTaskCreate(&PowerService::taskEntry,
		"power_service",
		4096,
		this,
		tskIDLE_PRIORITY + 2,
		&task_handle_);

	if (result != pdPASS) {
		task_handle_ = nullptr;
		ESP_LOGE(TAG, "Failed to create power service task");
		return false;
	}

	return true;
}

void PowerService::taskEntry(void *arg)
{
	static_cast<PowerService *>(arg)->runTask();
}

void PowerService::runTask()
{
	/* Report startup reason once when task starts. */
	const esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
	if (wakeup_cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
		ServiceEvent wakeup_event{};
		wakeup_event.origin = EventOrigin::Hardware;
		wakeup_event.event_id = static_cast<uint32_t>(ServiceEventId::PowerWakeupCause);
		wakeup_event.param = static_cast<uint32_t>(wakeup_cause);
		(void)postEvent(wakeup_event, 0);
		ESP_LOGI(TAG, "Reported wakeup cause=%u", static_cast<unsigned int>(wakeup_cause));
	} else {
		const esp_reset_reason_t reset_reason = esp_reset_reason();
		ServiceEvent reset_event{};
		reset_event.origin = EventOrigin::Hardware;
		reset_event.event_id = static_cast<uint32_t>(ServiceEventId::PowerResetReason);
		reset_event.param = static_cast<uint32_t>(reset_reason);
		(void)postEvent(reset_event, 0);
		ESP_LOGI(TAG, "Reported reset reason=%u", static_cast<unsigned int>(reset_reason));
	}

	/* Wait for service-manager command to enter sleep. */
	ServiceCommand command{};
	while (true) {
		if (!waitForCommand(&command, portMAX_DELAY)) {
			continue;
		}

		if (command.command_id == static_cast<uint32_t>(ServiceCommandId::EnterSleep)) {
			ESP_LOGI(TAG, "Received EnterSleep command");
			ESP_LOGI(TAG, "Configuring wakeup sources");
			setupWakeupSources();
			ESP_LOGI(TAG, "Entering deep sleep");
			esp_deep_sleep_start();
		}
	}
}

bool PowerService::postEvent(const ServiceEvent &event, TickType_t timeout_ticks)
{
	return xQueueSend(*event_queue_, &event, timeout_ticks) == pdTRUE;
}

bool PowerService::waitForCommand(ServiceCommand *command, TickType_t timeout_ticks)
{
	if (command == nullptr) {
		return false;
	}

	return xQueueReceive(*command_queue_, command, timeout_ticks) == pdTRUE;
}

QueueHandle_t *PowerService::getEventQueue() const
{
	return event_queue_;
}

QueueHandle_t *PowerService::getCommandQueue() const
{
	return command_queue_;
}

} // namespace esp32_camera_solutions
