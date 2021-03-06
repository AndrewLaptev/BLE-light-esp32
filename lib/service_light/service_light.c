#include "service_light.h"

light_mode_t consensus_light_set(connections_db_t *db) {
    light_mode_t light_mode_cons = {
        .light_warm_duty  =  0,
        .light_cold_duty  =  0,
        .light_brightness =  0,
        .color_temperature = 0
    };
    int check_connect_counter = 0;
    for (int i = 0; i < db->sum_connections; i++) {
        if (db->connections[i].light_mode.light_brightness != -1) {
            light_mode_cons.light_brightness += db->connections[i].light_mode.light_brightness;
            light_mode_cons.light_warm_duty += db->connections[i].light_mode.light_warm_duty;
            light_mode_cons.light_cold_duty += db->connections[i].light_mode.light_cold_duty;
            light_mode_cons.color_temperature += db->connections[i].light_mode.color_temperature;
            check_connect_counter++;
        }
    }
    if (check_connect_counter == 0) {
        return light_mode_cons;
    }
    light_mode_cons.light_brightness = (int)((float)light_mode_cons.light_brightness / (float)check_connect_counter);
    light_mode_cons.color_temperature = (int)((float)light_mode_cons.color_temperature / (float)check_connect_counter);
    light_mode_cons.light_warm_duty = (int)((float)light_mode_cons.light_warm_duty / (float)check_connect_counter);
    light_mode_cons.light_cold_duty = (int)((float)light_mode_cons.light_cold_duty / (float)check_connect_counter);
    return light_mode_cons;
}

bool parser_light_mode(char *buffer, int *input_brightness, int *input_color_temp) {
    char *buff_arr[PARSER_BUFF_ARR_MAX_LEN];
    char *token;
    int buff_arr_idx = 0;
    bool check_brightness = false;
    bool check_color_temp = false;

    token = strtok(buffer, "/");
    buff_arr[buff_arr_idx] = token;

    while(token != NULL) {
        buff_arr_idx++;
        token = strtok(NULL, "/");
        buff_arr[buff_arr_idx] = token;
    }

    if (buff_arr_idx == PARSER_BUFF_ARR_MAX_LEN) {
        check_brightness = (!(sscanf(buff_arr[0], "%d", input_brightness) == 0 || sscanf(buff_arr[0], "%d", input_brightness) == -1) 
                                    && *input_brightness >= 0);
        check_color_temp = (!(sscanf(buff_arr[1], "%d", input_color_temp) == 0 || sscanf(buff_arr[1], "%d", input_color_temp) == -1) 
                                    && ((*input_color_temp >= LEDC_COLOR_TEMP_MIN && *input_color_temp <= LEDC_COLOR_TEMP_MAX)
                                    || *input_color_temp == 0));
        if (check_brightness && check_color_temp) {
            return true;
        }
    }
    return false;
}

void gatts_profile_light_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(GATT_LIGHT_TAG, "REGISTER_APP_EVT, status %d, app_id %d\n", param->reg.status, param->reg.app_id);
        gl_service_tab[SERVICE_LIGHT_APP_ID].service_id.is_primary = true;
        gl_service_tab[SERVICE_LIGHT_APP_ID].service_id.id.inst_id = 0x00;
        gl_service_tab[SERVICE_LIGHT_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_service_tab[SERVICE_LIGHT_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_LIGHT;

        esp_ble_gatts_create_service(gatts_if, &gl_service_tab[SERVICE_LIGHT_APP_ID].service_id, GATTS_NUM_HANDLE_LIGHT);
        break;
    case ESP_GATTS_READ_EVT: {
        ESP_LOGI(GATT_LIGHT_TAG, "GATT_READ_EVT, conn_id %d, trans_id %d, handle %d\n", param->read.conn_id, param->read.trans_id, param->read.handle);
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = 4;
        rsp.attr_value.value[0] = 0xde;
        rsp.attr_value.value[1] = 0xed;
        rsp.attr_value.value[2] = 0xbe;
        rsp.attr_value.value[3] = 0xef;
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_OK, &rsp);
        break;
    }
    case ESP_GATTS_WRITE_EVT: {
        ESP_LOGI(GATT_LIGHT_TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %d, handle %d\n", param->write.conn_id, param->write.trans_id, param->write.handle);
        if (!param->write.is_prep){
            ESP_LOGI(GATT_LIGHT_TAG, "GATT_WRITE_EVT, value len %d, value :", param->write.len);

            err_connect err = check_connection_db(&connect_db, param);
            if(err == ERR_CONNECT_EXIST) {
                char buffer[LIGHT_MSG_BUFFER_LEN];
                memset(buffer, '\0', LIGHT_MSG_BUFFER_LEN);
                int input_brightness = 0;
                int input_color_temp = 0;
                light_mode_t light_mode;

                for(short i = 0; i < param->write.len; i++) {
                    buffer[i] = param->write.value[i];
                }

                if (parser_light_mode(buffer, &input_brightness, &input_color_temp)) {
                    ESP_LOGI(GATT_LIGHT_TAG, "GATT_WRITE_EVT, Light mode msg - br:%d tmp:%d", input_brightness, input_color_temp);
                    ledc_set_brightness(input_brightness, &light_mode);
                    ledc_set_color(input_color_temp, &light_mode);
                    
                    write_light_mode_to_db(&connect_db, param, &light_mode);
                    show_db(&connect_db, DB_MAX_SHOW_ROWS);

                    light_mode_cons = consensus_light_set(&connect_db);

                    ledc_fade_control(light_mode_cons.light_warm_duty, light_mode_cons.light_cold_duty);
                    ESP_LOGI(GATT_LIGHT_TAG, "GATT_WRITE_EVT, Light consensus mode - br:%d tmp:%d",
                                            light_mode_cons.light_brightness, light_mode_cons.color_temperature);
                } else {
                    show_db(&connect_db, DB_MAX_SHOW_ROWS);
                    ESP_LOGW(GATT_LIGHT_TAG, "GATT_WRITE_EVT, Write light mode in %s: Incorrect light mode message!", __func__);
                    ESP_LOGI(GATT_LIGHT_TAG, "GATT_WRITE_EVT, Light consensus mode - br:%d tmp:%d",
                                            light_mode_cons.light_brightness, light_mode_cons.color_temperature);
                }
            } else {
                ESP_LOGW(GATT_LIGHT_TAG, "GATT_WRITE_EVT, Unauthorizade message %s: %s", __func__, err_connect_check(err));
            }

            if (gl_service_tab[SERVICE_LIGHT_APP_ID].descr_handle == param->write.handle && param->write.len == 2){
                uint16_t descr_value= param->write.value[1]<<8 | param->write.value[0];
                if (descr_value == 0x0001){
                    if (b_property & ESP_GATT_CHAR_PROP_BIT_NOTIFY){
                        ESP_LOGI(GATT_LIGHT_TAG, "notify enable");
                        uint8_t notify_data[15];
                        for (int i = 0; i < sizeof(notify_data); ++i)
                        {
                            notify_data[i] = i%0xff;
                        }
                        //the size of notify_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_service_tab[SERVICE_LIGHT_APP_ID].char_handle,
                                                sizeof(notify_data), notify_data, false);
                    }
                }else if (descr_value == 0x0002){
                    if (b_property & ESP_GATT_CHAR_PROP_BIT_INDICATE){
                        ESP_LOGI(GATT_LIGHT_TAG, "indicate enable");
                        uint8_t indicate_data[15];
                        for (int i = 0; i < sizeof(indicate_data); ++i)
                        {
                            indicate_data[i] = i%0xff;
                        }
                        //the size of indicate_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_service_tab[SERVICE_LIGHT_APP_ID].char_handle,
                                                sizeof(indicate_data), indicate_data, true);
                    }
                }
                else if (descr_value == 0x0000){
                    ESP_LOGI(GATT_LIGHT_TAG, "notify/indicate disable ");
                }else{
                    ESP_LOGE(GATT_LIGHT_TAG, "unknown value");
                }
            }
        }
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        break;
    }
    case ESP_GATTS_EXEC_WRITE_EVT:
        ESP_LOGW(GATT_LIGHT_TAG,"ESP_GATTS_EXEC_WRITE_EVT, Too long message");
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        break;
    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(GATT_LIGHT_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
        break;
    case ESP_GATTS_UNREG_EVT:
        break;
    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(GATT_LIGHT_TAG, "CREATE_SERVICE_EVT, status %d,  service_handle %d\n", param->create.status, param->create.service_handle);
        gl_service_tab[SERVICE_LIGHT_APP_ID].service_handle = param->create.service_handle;
        gl_service_tab[SERVICE_LIGHT_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
        gl_service_tab[SERVICE_LIGHT_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_LIGHT;

        esp_ble_gatts_start_service(gl_service_tab[SERVICE_LIGHT_APP_ID].service_handle);
        b_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
        esp_err_t add_char_ret =esp_ble_gatts_add_char( gl_service_tab[SERVICE_LIGHT_APP_ID].service_handle, &gl_service_tab[SERVICE_LIGHT_APP_ID].char_uuid,
                                                        ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                        b_property,
                                                        NULL, NULL);
        if (add_char_ret){
            ESP_LOGE(GATT_LIGHT_TAG, "add char failed, error code =%x",add_char_ret);
        }
        break;
    case ESP_GATTS_ADD_INCL_SRVC_EVT:
        break;
    case ESP_GATTS_ADD_CHAR_EVT:
        ESP_LOGI(GATT_LIGHT_TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d\n",
                 param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);

        gl_service_tab[SERVICE_LIGHT_APP_ID].char_handle = param->add_char.attr_handle;
        gl_service_tab[SERVICE_LIGHT_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
        gl_service_tab[SERVICE_LIGHT_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_ble_gatts_add_char_descr(gl_service_tab[SERVICE_LIGHT_APP_ID].service_handle, &gl_service_tab[SERVICE_LIGHT_APP_ID].descr_uuid,
                                     ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                     NULL, NULL);
        break;
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        gl_service_tab[SERVICE_LIGHT_APP_ID].descr_handle = param->add_char_descr.attr_handle;
        ESP_LOGI(GATT_LIGHT_TAG, "ADD_DESCR_EVT, status %d, attr_handle %d, service_handle %d\n",
                 param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
        break;
    case ESP_GATTS_DELETE_EVT:
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGI(GATT_LIGHT_TAG, "SERVICE_START_EVT, status %d, service_handle %d\n",
                 param->start.status, param->start.service_handle);
        break;
    case ESP_GATTS_STOP_EVT:
        break;
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(GATT_LIGHT_TAG, "CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
                 param->connect.conn_id,
                 param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                 param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
        gl_service_tab[SERVICE_LIGHT_APP_ID].conn_id = param->connect.conn_id;
        break;
    case ESP_GATTS_CONF_EVT:
        ESP_LOGI(GATT_LIGHT_TAG, "ESP_GATTS_CONF_EVT status %d attr_handle %d", param->conf.status, param->conf.handle);
        if (param->conf.status != ESP_GATT_OK){
            esp_log_buffer_hex(GATT_LIGHT_TAG, param->conf.value, param->conf.len);
        }
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(GATT_LIGHT_TAG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
        esp_ble_gap_start_advertising(&adv_params);
        
        err_connect err = remove_connection_from_db(&connect_db, param);
        if (err == ERR_CONNECT_NOT_EXIST) {
            ESP_LOGW(GATT_LIGHT_TAG, "ESP_GATTS_DISCONNECT_EVT, Remove connection from DB in %s: %s", __func__, err_connect_check(err));
        }
        show_db(&connect_db, DB_MAX_SHOW_ROWS);
        light_mode_cons = consensus_light_set(&connect_db);
        ledc_fade_control(light_mode_cons.light_warm_duty, light_mode_cons.light_cold_duty);
        ESP_LOGI(GATT_LIGHT_TAG, "GATT_WRITE_EVT, Light consensus mode - br:%d tmp:%d",
                                        light_mode_cons.light_brightness, light_mode_cons.color_temperature);
        break;
    case ESP_GATTS_OPEN_EVT:
    case ESP_GATTS_CANCEL_OPEN_EVT:
    case ESP_GATTS_CLOSE_EVT:
    case ESP_GATTS_LISTEN_EVT:
    case ESP_GATTS_CONGEST_EVT:
    default:
        break;
    }
}