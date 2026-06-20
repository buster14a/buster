#include <buster/ui_builder.h>

UI_Signal ui_button(String8 string)
{
    ui_set_next_pref_width(ui_text_dim(8.0f, 1.0f));
    ui_set_next_pref_height(ui_percentage(1.0f, 0.0f));
    UI_Widget* widget = ui_widget_make((UI_WidgetFlags) {
        .draw_text = 1,
        .draw_background = 1,
        .draw_border = 1,
        .draw_hot_effects = 1,
        .draw_active_effects = 1,
        .mouse_clickable = 1,
        .keyboard_pressable = 1,
    }, string);

    UI_Signal signal = ui_signal_from_widget(widget);
    return signal;
}
