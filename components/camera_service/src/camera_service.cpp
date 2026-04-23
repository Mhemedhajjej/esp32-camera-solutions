#include "camera_service.h"

#include <cstdlib>

#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace esp32_camera_solutions {

static const char *TAG = "camera_service";

namespace {

static camera_config_t buildCameraConfig()
{
	camera_config_t config{};
	const bool psram_ready =
#if CONFIG_SPIRAM
		true;
#else
		false;
#endif

	config.ledc_channel = LEDC_CHANNEL_0;
	config.ledc_timer = LEDC_TIMER_0;
	config.pin_d0 = CONFIG_CAMERA_SERVICE_PIN_D0;
	config.pin_d1 = CONFIG_CAMERA_SERVICE_PIN_D1;
	config.pin_d2 = CONFIG_CAMERA_SERVICE_PIN_D2;
	config.pin_d3 = CONFIG_CAMERA_SERVICE_PIN_D3;
	config.pin_d4 = CONFIG_CAMERA_SERVICE_PIN_D4;
	config.pin_d5 = CONFIG_CAMERA_SERVICE_PIN_D5;
	config.pin_d6 = CONFIG_CAMERA_SERVICE_PIN_D6;
	config.pin_d7 = CONFIG_CAMERA_SERVICE_PIN_D7;
	config.pin_xclk = CONFIG_CAMERA_SERVICE_PIN_XCLK;
	config.pin_pclk = CONFIG_CAMERA_SERVICE_PIN_PCLK;
	config.pin_vsync = CONFIG_CAMERA_SERVICE_PIN_VSYNC;
	config.pin_href = CONFIG_CAMERA_SERVICE_PIN_HREF;
	config.pin_sccb_sda = CONFIG_CAMERA_SERVICE_PIN_SIOD;
	config.pin_sccb_scl = CONFIG_CAMERA_SERVICE_PIN_SIOC;
	config.pin_pwdn = CONFIG_CAMERA_SERVICE_PIN_PWDN;
	config.pin_reset = CONFIG_CAMERA_SERVICE_PIN_RESET;
	config.xclk_freq_hz = CONFIG_CAMERA_SERVICE_XCLK_FREQ_HZ;
	config.pixel_format = PIXFORMAT_JPEG;
	config.frame_size = psram_ready ? FRAMESIZE_VGA : FRAMESIZE_QVGA;
	config.jpeg_quality = CONFIG_CAMERA_SERVICE_JPEG_QUALITY;
	config.fb_count = psram_ready
		? CONFIG_CAMERA_SERVICE_FRAME_BUF_COUNT
		: 1;
	config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
	config.fb_location = psram_ready ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

	if (!psram_ready) {
		ESP_LOGW(TAG,
			"PSRAM is not initialized, using reduced camera config: frame_size=QVGA fb_count=1 in DRAM");
	}
	return config;
}

static ServiceEvent makeEvent(ServiceEventId event_id, uintptr_t data_ptr)
{
	ServiceEvent event{};
	event.origin = EventOrigin::CameraService;
	event.event_id = static_cast<uint32_t>(event_id);
	event.data_ptr = data_ptr;
	return event;
}

} // namespace

CameraService::CameraService() = default;

CameraService &CameraService::Get()
{
	static CameraService instance;
	return instance;
}

bool CameraService::init(QueueHandle_t *event_queue, QueueHandle_t *command_queue)
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

bool CameraService::start()
{
	if (task_handle_ != nullptr) {
		return true;
	}

	const BaseType_t result = xTaskCreate(&CameraService::taskEntry,
		"camera_service",
		4096,
		this,
		tskIDLE_PRIORITY + 2,
		&task_handle_);

	if (result != pdPASS) {
		task_handle_ = nullptr;
		ESP_LOGE(TAG, "Failed to create camera service task");
		return false;
	}

	return true;
}

bool CameraService::postEvent(const ServiceEvent &event, TickType_t timeout_ticks)
{
	return xQueueSend(*event_queue_, &event, timeout_ticks) == pdTRUE;
}

bool CameraService::waitForCommand(ServiceCommand *command, TickType_t timeout_ticks)
{
	/* guard against invalid destination pointer */
	if (command == nullptr) {
		return false;
	}

	/* block until a command is available or timeout expires */
	return xQueueReceive(*command_queue_, command, timeout_ticks) == pdTRUE;
}

QueueHandle_t *CameraService::getEventQueue() const
{
	return event_queue_;
}

QueueHandle_t *CameraService::getCommandQueue() const
{
	return command_queue_;
}

void CameraService::taskEntry(void *arg)
{
	static_cast<CameraService *>(arg)->runTask();
}

void CameraService::runTask()
{
	camera_config_t camera_config{};
	esp_err_t init_result = ESP_FAIL;
	ServiceEvent event{};
	ServiceCommand command{};
	ServiceCommandId cmd_id = ServiceCommandId::Unknown;
	camera_fb_t *frame = nullptr;
	CaptureFramePayload *payload = nullptr;
	ServiceEvent frame_ready{};

	ESP_LOGI(TAG, "Camera service task started");

	camera_config = buildCameraConfig();
	init_result = esp_camera_init(&camera_config);
	if (init_result != ESP_OK) {
		ESP_LOGE(TAG, "esp_camera_init failed: %s", esp_err_to_name(init_result));
		event = {};
		event.origin = EventOrigin::CameraService;
		event.event_id = static_cast<uint32_t>(ServiceEventId::CameraError);
		event.data_ptr = static_cast<uintptr_t>(init_result);
		(void)postEvent(event, 0);
		vTaskDelete(nullptr);
		return;
	}

	ESP_LOGI(TAG, "Camera initialized successfully");

	while (true) {
		/* wait for service-manager command */
		if (!waitForCommand(&command, portMAX_DELAY)) {
			continue;
		}

		/* dispatch command by id */
		cmd_id = static_cast<ServiceCommandId>(command.command_id);
		switch (cmd_id) {
		case ServiceCommandId::CaptureFrame:
			ESP_LOGI(TAG, "CaptureFrame command received");
			{
				/* capture one frame from the camera driver */
				frame = esp_camera_fb_get();
				if (frame == nullptr) {
					ESP_LOGE(TAG, "Failed to capture frame");
					(void)postEvent(makeEvent(ServiceEventId::CameraError, static_cast<uint32_t>(ESP_FAIL)), 0);
					break;
				}

				ESP_LOGI(TAG,
					"Captured frame: len=%u width=%u height=%u format=%u",
					static_cast<unsigned int>(frame->len),
					static_cast<unsigned int>(frame->width),
					static_cast<unsigned int>(frame->height),
					static_cast<unsigned int>(frame->format));

				payload = static_cast<CaptureFramePayload *>(std::malloc(sizeof(CaptureFramePayload)));
				if (payload == nullptr) {
					ESP_LOGE(TAG, "Failed to allocate capture payload");
					esp_camera_fb_return(frame);
					(void)postEvent(makeEvent(ServiceEventId::CameraError, static_cast<uint32_t>(ESP_ERR_NO_MEM)), 0);
					break;
				}

				payload->frame_handle = reinterpret_cast<uintptr_t>(frame);
				payload->data_ptr = reinterpret_cast<uintptr_t>(frame->buf);
				payload->data_len = frame->len;
				payload->width = frame->width;
				payload->height = frame->height;
				payload->format = static_cast<uint32_t>(frame->format);

				/* post frame payload to service manager for next-stage handling */
				frame_ready = {};
				frame_ready.origin = EventOrigin::CameraService;
				frame_ready.event_id = static_cast<uint32_t>(ServiceEventId::CameraFrameReady);
				frame_ready.data_ptr = reinterpret_cast<uintptr_t>(payload);
				if (!postEvent(frame_ready, 0)) {
					ESP_LOGE(TAG, "Failed to post CameraFrameReady event");
					std::free(payload);
					esp_camera_fb_return(frame);
				}
			}
			break;

		case ServiceCommandId::ReleaseCaptureFrame:
			if (command.data_ptr == 0U) {
				ESP_LOGW(TAG, "ReleaseCaptureFrame received with null handle");
				break;
			}
			esp_camera_fb_return(reinterpret_cast<camera_fb_t *>(static_cast<uintptr_t>(command.data_ptr)));
			break;

		case ServiceCommandId::StartStream:
			/* TODO: start MJPEG/RTSP stream */
			ESP_LOGI(TAG, "StartStream command received");
			break;

		case ServiceCommandId::StopStream:
			/* TODO: stop active stream */
			ESP_LOGI(TAG, "StopStream command received");
			break;

		default:
			ESP_LOGW(TAG, "Unknown command id=%u", command.command_id);
			break;
		}
	}
}

} // namespace esp32_camera_solutions
