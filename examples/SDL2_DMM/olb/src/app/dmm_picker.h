/**
 * DMM picker — keyboard-driven 320x170 meter+port selection UI.
 * Runs the compact DMM view in-process for the chosen driver/port.
 */
#ifndef APP_DMM_PICKER_H
#define APP_DMM_PICKER_H

/* Show the picker; loops until the user quits. Returns 0 on clean exit. */
int dmm_picker_run(void);

#endif
