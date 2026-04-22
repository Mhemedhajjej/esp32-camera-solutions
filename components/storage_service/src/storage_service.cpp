#include "storage_service.h"

#include <cstdio>
#include <cstdlib>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

namespace esp32_camera_solutions {

static const char *TAG = "storage_service";

namespace {

static constexpr const char *kSdCardBasePath = "/sdcard";

static esp_err_t mountSdCard(sdmmc_card_t **card)
{
	sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	host.flags = SDMMC_HOST_FLAG_1BIT;

	sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
	slot_config.width = 1;

	esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
	mount_config.format_if_mount_failed = false;
	mount_config.max_files = 8;
	mount_config.allocation_unit_size = 16 * 1024;

	return esp_vfs_fat_sdmmc_mount(kSdCardBasePath, &host, &slot_config, &mount_config, card);
}

static bool ensureStorageMounted()
{
	static bool mounted = false;
	static sdmmc_card_t *card = nullptr;
	if (mounted) {
		return true;
	}

	esp_err_t err = mountSdCard(&card);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to mount SD card at %s (%s)",
			kSdCardBasePath,
			esp_err_to_name(err));
		return false;
	}

	if (card != nullptr) {
		sdmmc_card_print_info(stdout, card);
	}
    ESP_LOGI(TAG, "SD card mounted at %s", kSdCardBasePath);

	mounted = true;
	return true;
}

static bool writeFrameToStorage(const ServiceCommand &command)
{
	if (command.data_ptr == 0U) {
		ESP_LOGE(TAG, "Invalid StoreCapture payload: ptr=%p len=%u",
			reinterpret_cast<void *>(command.data_ptr),
			static_cast<unsigned int>(command.data_len));
		return false;
	}

	if (!ensureStorageMounted()) {
		return false;
	}

	char file_path[96] = {0};
	const uint64_t ts_us = static_cast<uint64_t>(esp_timer_get_time());
	const int path_result = std::snprintf(file_path,
		sizeof(file_path),
		"%s/capture_%llu.jpg",
		kSdCardBasePath,
		static_cast<unsigned long long>(ts_us));

	if ((path_result < 0) || (path_result >= static_cast<int>(sizeof(file_path)))) {
		ESP_LOGE(TAG, "Failed to build storage file path");
		return false;
	}

	FILE *file = std::fopen(file_path, "wb");
	if (file == nullptr) {
		ESP_LOGE(TAG, "Failed to open file for write: %s", file_path);
		return false;
	}

	const uint8_t *frame_buf = reinterpret_cast<const uint8_t *>(command.data_ptr);
	const size_t bytes_to_write = static_cast<size_t>(command.data_len);
	const size_t bytes_written = std::fwrite(frame_buf, 1, bytes_to_write, file);
	std::fclose(file);

	if (bytes_written != bytes_to_write) {
		ESP_LOGE(TAG, "Partial frame write: expected=%u wrote=%u",
			static_cast<unsigned int>(bytes_to_write),
			static_cast<unsigned int>(bytes_written));
		return false;
	}

	ESP_LOGI(TAG,
		"Stored frame to %s (len=%u width=%u height=%u format=%u)",
		file_path,
		static_cast<unsigned int>(command.data_len),
		static_cast<unsigned int>(command.width),
		static_cast<unsigned int>(command.height),
		static_cast<unsigned int>(command.format));
	return true;
}

static ServiceEvent makeEvent(ServiceEventId event_id, uintptr_t data_ptr)
{
	ServiceEvent event{};
	event.origin = EventOrigin::Component;
	event.event_id = static_cast<uint32_t>(event_id);
	event.data_ptr = data_ptr;
	return event;
}

} // namespace

StorageService::StorageService() = default;

StorageService &StorageService::Get()
{
	static StorageService instance;
	return instance;
}

bool StorageService::init(QueueHandle_t *event_queue, QueueHandle_t *command_queue)
{
	if ((event_queue == nullptr) || (command_queue == nullptr)) {
		return false;
	}

	event_queue_ = event_queue;
	command_queue_ = command_queue;

	if ((*event_queue_ == nullptr) || (*command_queue_ == nullptr)) {
		return false;
	}

	return start();
}

bool StorageService::start()
{
	if (task_handle_ != nullptr) {
		return true;
	}

	const BaseType_t result = xTaskCreate(&StorageService::taskEntry,
		"storage_service",
		4096,
		this,
		tskIDLE_PRIORITY + 2,
		&task_handle_);

	if (result != pdPASS) {
		task_handle_ = nullptr;
		ESP_LOGE(TAG, "Failed to create storage service task");
		return false;
	}

	return true;
}

bool StorageService::postEvent(const ServiceEvent &event, TickType_t timeout_ticks)
{
	return xQueueSend(*event_queue_, &event, timeout_ticks) == pdTRUE;
}

bool StorageService::waitForCommand(ServiceCommand *command, TickType_t timeout_ticks)
{
	if (command == nullptr) {
		return false;
	}
	return xQueueReceive(*command_queue_, command, timeout_ticks) == pdTRUE;
}

QueueHandle_t *StorageService::getEventQueue() const
{
	return event_queue_;
}

QueueHandle_t *StorageService::getCommandQueue() const
{
	return command_queue_;
}

void StorageService::taskEntry(void *arg)
{
	static_cast<StorageService *>(arg)->runTask();
}

void StorageService::runTask()
{
	ESP_LOGI(TAG, "Storage service task started");

	ServiceCommand command{};
	while (true) {
		if (!waitForCommand(&command, portMAX_DELAY)) {
			continue;
		}

		const auto cmd_id = static_cast<ServiceCommandId>(command.command_id);
		switch (cmd_id) {
		case ServiceCommandId::StoreCapture:
			ESP_LOGI(TAG, "StoreCapture command received, frame_len=%u",
				static_cast<unsigned int>(command.data_len));
			if (writeFrameToStorage(command)) {
				(void)postEvent(makeEvent(ServiceEventId::StorageWriteDone, command.param), 0);
			} else {
				(void)postEvent(makeEvent(ServiceEventId::StorageError, command.param), 0);
			}
			break;

		default:
			ESP_LOGW(TAG, "Unknown command id=%u", command.command_id);
			break;
		}
	}
}

} // namespace esp32_camera_solutions
