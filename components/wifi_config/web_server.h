#ifndef WEB_SERVER_H
#define WEB_SERVER_H

// bool start_web_server();
void start_web_server();
void save_wifi_config(const char *ssid, const char *password, const char *dir);
void wait_for_wifi_credentials();
#endif /* WEB_SERVER_H */