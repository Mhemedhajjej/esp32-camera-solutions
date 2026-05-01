#include "wifi_service.h"

#include <cstdlib>
#include <cstring>

#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace esp32_camera_solutions {

static const char *TAG = "wifi_service";

namespace {

static constexpr int kWifiConnectedBit = BIT0;
static constexpr int kWifiFailedBit = BIT1;

struct WifiRuntimeContext {
	EventGroupHandle_t event_group = nullptr;
	int retry_count = 0;
};

static void wifiEventHandler(void *arg,
	esp_event_base_t event_base,
	int32_t event_id,
	void *event_data)
{
	(void)event_data;
	WifiRuntimeContext *ctx = static_cast<WifiRuntimeContext *>(arg);
	if ((ctx == nullptr) || (ctx->event_group == nullptr)) {
		return;
	}

	if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
		esp_wifi_connect();
		return;
	}

	if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
		if (ctx->retry_count < CONFIG_WIFI_SERVICE_MAX_RETRY) {
			ctx->retry_count++;
			esp_wifi_connect();
			ESP_LOGW(TAG, "WiFi disconnected, retry %d/%d",
				ctx->retry_count,
				CONFIG_WIFI_SERVICE_MAX_RETRY);
		} else {
			xEventGroupSetBits(ctx->event_group, kWifiFailedBit);
		}
		return;
	}

	if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
		ctx->retry_count = 0;
		xEventGroupSetBits(ctx->event_group, kWifiConnectedBit);
	}
}

static ServiceEvent makeEvent(ServiceEventId event_id, uintptr_t data_ptr)
{
	ServiceEvent event{};
	event.origin = EventOrigin::WifiService;
	event.event_id = static_cast<uint32_t>(event_id);
	event.data_ptr = data_ptr;
	return event;
}

} // namespace

WifiService::WifiService() = default;

WifiService &WifiService::Get()
{
	static WifiService instance;
	return instance;
}

bool WifiService::init(QueueHandle_t *event_queue, QueueHandle_t *command_queue)
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

bool WifiService::start()
{
	if (task_handle_ != nullptr) {
		return true;
	}

	const BaseType_t result = xTaskCreate(&WifiService::taskEntry,
		"wifi_service",
		6144,
		this,
		tskIDLE_PRIORITY + 2,
		&task_handle_);

	if (result != pdPASS) {
		task_handle_ = nullptr;
		ESP_LOGE(TAG, "Failed to create wifi service task");
		return false;
	}

	return true;
}

bool WifiService::postEvent(const ServiceEvent &event, TickType_t timeout_ticks)
{
	return xQueueSend(*event_queue_, &event, timeout_ticks) == pdTRUE;
}

bool WifiService::waitForCommand(ServiceCommand *command, TickType_t timeout_ticks)
{
	if (command == nullptr) {
		return false;
	}
	return xQueueReceive(*command_queue_, command, timeout_ticks) == pdTRUE;
}

QueueHandle_t *WifiService::getEventQueue() const
{
	return event_queue_;
}

QueueHandle_t *WifiService::getCommandQueue() const
{
	return command_queue_;
}

void WifiService::taskEntry(void *arg)
{
	static_cast<WifiService *>(arg)->runTask();
}

bool WifiService::ensureConnected()
{
	static WifiRuntimeContext runtime{};
	static esp_event_handler_instance_t wifi_event_instance = nullptr;
	static esp_event_handler_instance_t got_ip_instance = nullptr;

	if (wifi_ready_) {
		EventBits_t bits = xEventGroupGetBits(runtime.event_group);
		if ((bits & kWifiConnectedBit) != 0) {
			return true;
		}
	}

	if (runtime.event_group == nullptr) {
		runtime.event_group = xEventGroupCreate();
		if (runtime.event_group == nullptr) {
			ESP_LOGE(TAG, "Failed to create WiFi event group");
			return false;
		}
	}

	if (!wifi_ready_) {
		esp_err_t err = nvs_flash_init();
		if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
			ESP_ERROR_CHECK(nvs_flash_erase());
			err = nvs_flash_init();
		}
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
			return false;
		}

		ESP_ERROR_CHECK(esp_netif_init());
		ESP_ERROR_CHECK(esp_event_loop_create_default());
		esp_netif_create_default_wifi_sta();

		wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
		ESP_ERROR_CHECK(esp_wifi_init(&cfg));

		ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
			ESP_EVENT_ANY_ID,
			&wifiEventHandler,
			&runtime,
			&wifi_event_instance));
		ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
			IP_EVENT_STA_GOT_IP,
			&wifiEventHandler,
			&runtime,
			&got_ip_instance));

		wifi_config_t wifi_config = {};
		std::strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid),
			CONFIG_WIFI_SERVICE_SSID,
			sizeof(wifi_config.sta.ssid) - 1);
		std::strncpy(reinterpret_cast<char *>(wifi_config.sta.password),
			CONFIG_WIFI_SERVICE_PASSWORD,
			sizeof(wifi_config.sta.password) - 1);
		wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
		wifi_config.sta.pmf_cfg.capable = true;
		wifi_config.sta.pmf_cfg.required = false;

		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
		ESP_ERROR_CHECK(esp_wifi_start());
		wifi_ready_ = true;
	}

	runtime.retry_count = 0;
	xEventGroupClearBits(runtime.event_group, kWifiConnectedBit | kWifiFailedBit);
	ESP_ERROR_CHECK(esp_wifi_connect());

	const EventBits_t wait_bits = xEventGroupWaitBits(runtime.event_group,
		kWifiConnectedBit | kWifiFailedBit,
		pdFALSE,
		pdFALSE,
		pdMS_TO_TICKS(CONFIG_WIFI_SERVICE_CONNECT_TIMEOUT_MS));

	if ((wait_bits & kWifiConnectedBit) != 0) {
		ESP_LOGI(TAG, "Connected to WiFi SSID '%s'", CONFIG_WIFI_SERVICE_SSID);
		(void)postEvent(makeEvent(ServiceEventId::WifiConnected, 0U), 0);
		return true;
	}

	ESP_LOGE(TAG, "Failed to connect to WiFi SSID '%s'", CONFIG_WIFI_SERVICE_SSID);
	(void)postEvent(makeEvent(ServiceEventId::WifiError, 0U), 0);
	return false;
}

bool WifiService::uploadFrame(const CaptureFramePayload &payload)
{
	if ((payload.data_ptr == 0U) || (payload.data_len == 0U)) {
		ESP_LOGE(TAG, "Invalid upload payload");
		return false;
	}

	if (std::strlen(CONFIG_WIFI_SERVICE_UPLOAD_URL) == 0U) {
		ESP_LOGE(TAG, "Upload URL is empty, configure WIFI_SERVICE_UPLOAD_URL");
		return false;
	}

	esp_http_client_config_t config = {};
	config.url = CONFIG_WIFI_SERVICE_UPLOAD_URL;
	config.method = HTTP_METHOD_POST;
	config.timeout_ms = CONFIG_WIFI_SERVICE_HTTP_TIMEOUT_MS;

	esp_http_client_handle_t client = esp_http_client_init(&config);
	if (client == nullptr) {
		ESP_LOGE(TAG, "Failed to initialize HTTP client");
		return false;
	}

	esp_http_client_set_header(client, "Content-Type", "image/jpeg");
	esp_http_client_set_post_field(client,
		reinterpret_cast<const char *>(payload.data_ptr),
		static_cast<int>(payload.data_len));

	const esp_err_t err = esp_http_client_perform(client);
	const int status = esp_http_client_get_status_code(client);
	esp_http_client_cleanup(client);

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "HTTP upload failed: %s", esp_err_to_name(err));
		return false;
	}

	if ((status < 200) || (status >= 300)) {
		ESP_LOGE(TAG, "HTTP upload rejected: status=%d", status);
		return false;
	}

	ESP_LOGI(TAG,
		"Sent frame to receiver %s (len=%u width=%u height=%u status=%d)",
		CONFIG_WIFI_SERVICE_UPLOAD_URL,
		static_cast<unsigned int>(payload.data_len),
		static_cast<unsigned int>(payload.width),
		static_cast<unsigned int>(payload.height),
		status);
	return true;
}

void WifiService::runTask()
{
	ESP_LOGI(TAG, "WiFi service task started");

	if (std::strlen(CONFIG_WIFI_SERVICE_SSID) == 0U) {
		ESP_LOGE(TAG, "WiFi SSID is empty, configure WIFI_SERVICE_SSID");
	}

	ServiceCommand command{};
	CaptureFramePayload *payload = nullptr;
	uintptr_t frame_handle = 0U;
	bool upload_ok = false;

	while (true) {
		if (!waitForCommand(&command, portMAX_DELAY)) {
			continue;
		}

		switch (static_cast<ServiceCommandId>(command.command_id)) {
		case ServiceCommandId::UploadCapture:
			payload = reinterpret_cast<CaptureFramePayload *>(command.data_ptr);
			if (payload == nullptr) {
				ESP_LOGE(TAG, "UploadCapture command received with null payload");
				break;
			}

			frame_handle = payload->frame_handle;
			upload_ok = ensureConnected() && uploadFrame(*payload);
			std::free(payload);

			if (upload_ok) {
				(void)postEvent(makeEvent(ServiceEventId::WifiUploadDone, frame_handle), 0);
			} else {
				(void)postEvent(makeEvent(ServiceEventId::WifiUploadError, frame_handle), 0);
			}
			break;

		default:
			ESP_LOGW(TAG, "Unknown command id=%u", command.command_id);
			break;
		}
	}
}

} // namespace esp32_camera_solutions
