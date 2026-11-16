#ifndef P4_CALENDAR_SKIP_H
#define P4_CALENDAR_SKIP_H
void p4_calendar_sprite_state(void *pack, unsigned record);
/* Replay the last sprite's device state after omitting its render-only preparation. */
static void p4_calendar_restore_state(CalendarState *calendar, unsigned initialFlags)
{
    int record = -1, ordinary;
    if (initialFlags & 1) {
        ordinary = calendar->field_18 ? calendar->field_1C == 0 : FldStepIndex() == 0 && evtHasTaskHandle() == 0;
        record = ordinary ? 0x1D : 0x1E;
    }
    if (calendar->field_0C & 2) {
        record = calendar->period + 0x14;
        if (calHasDayEntry(calendar->daysSinceApr5) && calendar->period == 3)
            record = 0x1A;
        if (calendar->field_18 ? calendar->field_1C == 1 : FldStepIndex() != 0 || evtHasTaskHandle() != 0) {
            if (record == 0x18) record = 0x1B;
            else if (record == 0x1A) record = 0x1C;
        }
    }
    if (calendar->field_0C & 4) {
        ordinary = calendar->field_18 ? calendar->field_1C == 0 : FldStepIndex() == 0 && evtHasTaskHandle() == 0;
        if (ordinary) record = calendar->field_08 + 0x20;
    }
    if (record >= 0)
        p4_calendar_sprite_state((void *)calendar->field_110, record);
}
#endif
