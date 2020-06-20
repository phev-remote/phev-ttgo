
#ifndef OTA_FORCE
    #define OTA_FORCE "otaforce"
#endif

esp_err_t ota_do_firmware_upgrade(const char * url,const char * fallbackUrl);
char * ota_get_latest_version(const char * url, const char * fallbackUrl);