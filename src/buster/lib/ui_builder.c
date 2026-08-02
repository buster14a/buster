#include <buster/lib/ui_builder.h>

UI_Signal ui_button(String8 string)
{
    ui_set_next_pref_width(ui_text_dim(8.0f, 1.0f));
    ui_set_next_pref_height(ui_percentage(1.0f, 0.0f));
    UI_BoxFlags flags = UI_BoxFlag_DrawText | UI_BoxFlag_DrawBackground | UI_BoxFlag_DrawBorder | UI_BoxFlag_DrawHotEffects | UI_BoxFlag_DrawActiveEffects |
                        UI_BoxFlag_Clickable;
    UI_Box* box = ui_box_make(flags, string);

    UI_Signal signal = ui_signal_from_box(box);
    return signal;
}
