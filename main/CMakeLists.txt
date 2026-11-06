idf_component_register(
    SRCS "main.c"
         "heart_app.c"
         "heart_ble.c"
         "heart_model.c"
         "heart_store.c"
         "heart_ui.c"
         "fonts/heart_font_80.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_timer nvs_flash lvgl
    EMBED_FILES "assets/heart_bg.bin"
)
