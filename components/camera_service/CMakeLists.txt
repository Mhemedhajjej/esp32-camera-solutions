idf_component_register(
	SRCS "src/camera_service.cpp"
	INCLUDE_DIRS "include"
	REQUIRES service_manager esp32-camera
	PRIV_REQUIRES esp_psram
)
