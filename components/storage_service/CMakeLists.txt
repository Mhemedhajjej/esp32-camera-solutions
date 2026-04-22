idf_component_register(
	SRCS "src/storage_service.cpp"
	INCLUDE_DIRS "include"
	REQUIRES service_manager fatfs sdmmc esp_timer
)
