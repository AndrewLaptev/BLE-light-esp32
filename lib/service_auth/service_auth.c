#include "service_auth.h"

int access_token = ACCESS_TOKEN_VAL;

bool authorization_connection(char *auth_msg, int access_token) {
    int auth_num;
    sscanf(auth_msg, "%d", &auth_num);
    if (auth_num == access_token) { return true; }
    return false;
}

void gatts_profile_auth_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(GATT_AUTH_TAG, "REGISTER_APP_EVT, status %d, app_id %d\n", param->reg.status, param->reg.app_id);
        gl_service_tab[SERVICE_AUTH_APP_ID].service_id.is_primary = true;
        gl_service_tab[SERVICE_AUTH_APP_ID].service_id.id.inst_id = 0x00;
        gl_service_tab[SERVICE_AUTH_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        gl_service_tab[SERVICE_AUTH_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_AUTH;

        esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(DEVICE_NAME);
        if (set_dev_name_ret){
            ESP_LOGE(GATT_AUTH_TAG, "set device name failed, error code = %x", set_dev_name_ret);
        }
        //config adv data
        esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
        if (ret){
            ESP_LOGE(GATT_AUTH_TAG, "config adv data failed, error code = %x", ret);
        }
        adv_config_done |= ADV_CONFIG_FLAG;
        //config scan response data
        ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
        if (ret){
            ESP_LOGE(GATT_AUTH_TAG, "config scan response data failed, error code = %x", ret);
        }
        adv_config_done |= SCAN_RSP_CONFIG_FLAG;
        esp_ble_gatts_create_service(gatts_if, &gl_service_tab[SERVICE_AUTH_APP_ID].service_id, GATTS_NUM_HANDLE_AUTH);
        break;
    case ESP_GATTS_READ_EVT: {
        ESP_LOGI(GATT_AUTH_TAG, "GATT_READ_EVT, conn_id %d, trans_id %d, handle %d\n", param->read.conn_id, param->read.trans_id, param->read.handle);
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
        ESP_LOGI(GATT_AUTH_TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %d, handle %d", param->write.conn_id, param->write.trans_id, param->write.handle);
        if (!param->write.is_prep){
            ESP_LOGI(GATT_AUTH_TAG, "GATT_WRITE_EVT, value len %d, value :", param->write.len);
            
            char buffer[AUTH_MSG_BUFFER_LEN];
            memset(buffer, '\0', AUTH_MSG_BUFFER_LEN);

            for(short i = 0; i < param->write.len; i++) {
                buffer[i] = param->write.value[i];
            }

            if (authorization_connection(buffer, access_token)) {
                err_connect err = add_connection_to_db(&connect_db, param);
                if (err == ERR_CONNECT_NOT_EXIST) {
                    show_db(&connect_db, DB_MAX_SHOW_ROWS);
                } else {
                    ESP_LOGW(GATT_AUTH_TAG, "GATT_WRITE_EVT, Adding connection to DB in %s: %s\n", __func__, err_connect_check(err));
                }
            } else {
                ESP_LOGW(GATT_AUTH_TAG, "GATT_WRITE_EVT, Adding connection to DB in %s: Incorrect access token!\n", __func__);
            }

            if (gl_service_tab[SERVICE_AUTH_APP_ID].descr_handle == param->write.handle && param->write.len == 2){
                uint16_t descr_value = param->write.value[1]<<8 | param->write.value[0];
                if (descr_value == 0x0001){
                    if (a_property & ESP_GATT_CHAR_PROP_BIT_NOTIFY){
                        ESP_LOGI(GATT_AUTH_TAG, "notify enable");
                        uint8_t notify_data[15];
                        for (int i = 0; i < sizeof(notify_data); ++i)
                        {
                            notify_data[i] = i%0xff;
                        }
                        //the size of notify_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_service_tab[SERVICE_AUTH_APP_ID].char_handle,
                                                sizeof(notify_data), notify_data, false);
                    }
                }else if (descr_value == 0x0002){
                    if (a_property & ESP_GATT_CHAR_PROP_BIT_INDICATE){
                        ESP_LOGI(GATT_AUTH_TAG, "indicate enable");
                        uint8_t indicate_data[15];
                        for (int i = 0; i < sizeof(indicate_data); ++i)
                        {
                            indicate_data[i] = i%0xff;
                        }
                        //the size of indicate_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_service_tab[SERVICE_AUTH_APP_ID].char_handle,
                                                sizeof(indicate_data), indicate_data, true);
                    }
                }
                else if (descr_value == 0x0000){
                    ESP_LOGI(GATT_AUTH_TAG, "notify/indicate disable ");
                }else{
                    ESP_LOGE(GATT_AUTH_TAG, "unknown descr value");
                    esp_log_buffer_hex(GATT_AUTH_TAG, param->write.value, param->write.len);
                }
            }
        }
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        break;
    }
    case ESP_GATTS_EXEC_WRITE_EVT:
        ESP_LOGW(GATT_AUTH_TAG,"ESP_GATTS_EXEC_WRITE_EVT, Too long message");
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        break;
    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(GATT_AUTH_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
        break;
    case ESP_GATTS_UNREG_EVT:
        break;
    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(GATT_AUTH_TAG, "CREATE_SERVICE_EVT, status %d,  service_handle %d\n", param->create.status, param->create.service_handle);
        gl_service_tab[SERVICE_AUTH_APP_ID].service_handle = param->create.service_handle;
        gl_service_tab[SERVICE_AUTH_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
        gl_service_tab[SERVICE_AUTH_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_AUTH;

        esp_ble_gatts_start_service(gl_service_tab[SERVICE_AUTH_APP_ID].service_handle);
        a_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
        esp_err_t add_char_ret = esp_ble_gatts_add_char(gl_service_tab[SERVICE_AUTH_APP_ID].service_handle, &gl_service_tab[SERVICE_AUTH_APP_ID].char_uuid,
                                                        ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                        a_property,
                                                        &gatts_demo_char1_val, NULL);
        if (add_char_ret){
            ESP_LOGE(GATT_AUTH_TAG, "add char failed, error code =%x",add_char_ret);
        }
        break;
    case ESP_GATTS_ADD_INCL_SRVC_EVT:
        break;
    case ESP_GATTS_ADD_CHAR_EVT: {
        uint16_t length = 0;
        const uint8_t *prf_char;

        ESP_LOGI(GATT_AUTH_TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d\n",
                param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);
        gl_service_tab[SERVICE_AUTH_APP_ID].char_handle = param->add_char.attr_handle;
        gl_service_tab[SERVICE_AUTH_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
        gl_service_tab[SERVICE_AUTH_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_err_t get_attr_ret = esp_ble_gatts_get_attr_value(param->add_char.attr_handle,  &length, &prf_char);
        if (get_attr_ret == ESP_FAIL){
            ESP_LOGE(GATT_AUTH_TAG, "ILLEGAL HANDLE");
        }

        ESP_LOGI(GATT_AUTH_TAG, "the gatts demo char length = %x\n", length);
        for(int i = 0; i < length; i++){
            ESP_LOGI(GATT_AUTH_TAG, "prf_char[%x] =%x\n",i,prf_char[i]);
        }
        esp_err_t add_descr_ret = esp_ble_gatts_add_char_descr(gl_service_tab[SERVICE_AUTH_APP_ID].service_handle, &gl_service_tab[SERVICE_AUTH_APP_ID].descr_uuid,
                                                                ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        if (add_descr_ret){
            ESP_LOGE(GATT_AUTH_TAG, "add char descr failed, error code =%x", add_descr_ret);
        }
        break;
    }
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        gl_service_tab[SERVICE_AUTH_APP_ID].descr_handle = param->add_char_descr.attr_handle;
        ESP_LOGI(GATT_AUTH_TAG, "ADD_DESCR_EVT, status %d, attr_handle %d, service_handle %d\n",
                 param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
        break;
    case ESP_GATTS_DELETE_EVT:
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGI(GATT_AUTH_TAG, "SERVICE_START_EVT, status %d, service_handle %d\n",
                 param->start.status, param->start.service_handle);
        break;
    case ESP_GATTS_STOP_EVT:
        break;
    case ESP_GATTS_CONNECT_EVT: {
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        /* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
        conn_params.latency = 0;
        conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
        conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
        conn_params.timeout = 400;    // timeout = 400*10ms = 4000ms
        ESP_LOGI(GATT_AUTH_TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
                 param->connect.conn_id,
                 param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                 param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
        gl_service_tab[SERVICE_AUTH_APP_ID].conn_id = param->connect.conn_id;
        //start sent the update connection parameters to the peer device.
        esp_ble_gap_update_conn_params(&conn_params);
        esp_ble_gap_start_advertising(&adv_params);
        break;
    }
    case ESP_GATTS_DISCONNECT_EVT:
        break;
    case ESP_GATTS_CONF_EVT:
        ESP_LOGI(GATT_AUTH_TAG, "ESP_GATTS_CONF_EVT, status %d attr_handle %d", param->conf.status, param->conf.handle);
        if (param->conf.status != ESP_GATT_OK){
            esp_log_buffer_hex(GATT_AUTH_TAG, param->conf.value, param->conf.len);
        }
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