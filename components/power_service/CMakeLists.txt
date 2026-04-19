idf_component_register(
	SRCS "src/power_service.cpp"
	INCLUDE_DIRS "include"
	REQUIRES service_manager
	PRIV_REQUIRES esp_driver_gpio
)
