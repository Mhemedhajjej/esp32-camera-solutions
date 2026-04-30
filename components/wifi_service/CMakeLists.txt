idf_component_register(
	SRCS "src/wifi_service.cpp"
	INCLUDE_DIRS "include"
	REQUIRES service_manager esp_http_client esp_wifi esp_event esp_netif nvs_flash
)
